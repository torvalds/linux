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
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <endian.h>
#include <fcntl.h>
#include <errno.h>
#include <ctype.h>
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
#include <linux/version.h>
#include <sys/epoll.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/vfs.h>
#include <sys/utsname.h>
#include <sys/resource.h>
#include <tools/libc_compat.h>
#include <libelf.h>
#include <gelf.h>
#include <zlib.h>

#include "libbpf.h"
#include "bpf.h"
#include "btf.h"
#include "str_error.h"
#include "libbpf_internal.h"
#include "hashmap.h"

/* make sure libbpf doesn't use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

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

static struct bpf_map *bpf_object__add_map(struct bpf_object *obj);
static struct bpf_program *bpf_object__find_prog_by_idx(struct bpf_object *obj,
							int idx);
static const struct btf_type *
skip_mods_and_typedefs(const struct btf *btf, __u32 id, __u32 *res_id);

static int __base_pr(enum libbpf_print_level level, const char *format,
		     va_list args)
{
	if (level == LIBBPF_DEBUG)
		return 0;

	return vfprintf(stderr, format, args);
}

static libbpf_print_fn_t __libbpf_pr = __base_pr;

libbpf_print_fn_t libbpf_set_print(libbpf_print_fn_t fn)
{
	libbpf_print_fn_t old_print_fn = __libbpf_pr;

	__libbpf_pr = fn;
	return old_print_fn;
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

static void pr_perm_msg(int err)
{
	struct rlimit limit;
	char buf[100];

	if (err != -EPERM || geteuid() != 0)
		return;

	err = getrlimit(RLIMIT_MEMLOCK, &limit);
	if (err)
		return;

	if (limit.rlim_cur == RLIM_INFINITY)
		return;

	if (limit.rlim_cur < 1024)
		snprintf(buf, sizeof(buf), "%zu bytes", (size_t)limit.rlim_cur);
	else if (limit.rlim_cur < 1024*1024)
		snprintf(buf, sizeof(buf), "%.1f KiB", (double)limit.rlim_cur / 1024);
	else
		snprintf(buf, sizeof(buf), "%.1f MiB", (double)limit.rlim_cur / (1024*1024));

	pr_warn("permission error while running as root; try raising 'ulimit -l'? current value: %s\n",
		buf);
}

#define STRERR_BUFSIZE  128

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
	/* BPF_F_MMAPABLE is supported for arrays */
	__u32 array_mmap:1;
	/* BTF_FUNC_GLOBAL is supported */
	__u32 btf_func_global:1;
};

enum reloc_type {
	RELO_LD64,
	RELO_CALL,
	RELO_DATA,
	RELO_EXTERN,
};

struct reloc_desc {
	enum reloc_type type;
	int insn_idx;
	int map_idx;
	int sym_off;
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

	struct reloc_desc *reloc_desc;
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
	__u32 attach_btf_id;
	__u32 attach_prog_fd;
	void *func_info;
	__u32 func_info_rec_size;
	__u32 func_info_cnt;

	struct bpf_capabilities *caps;

	void *line_info;
	__u32 line_info_rec_size;
	__u32 line_info_cnt;
	__u32 prog_flags;
};

struct bpf_struct_ops {
	const char *tname;
	const struct btf_type *type;
	struct bpf_program **progs;
	__u32 *kern_func_off;
	/* e.g. struct tcp_congestion_ops in bpf_prog's btf format */
	void *data;
	/* e.g. struct bpf_struct_ops_tcp_congestion_ops in
	 *      btf_vmlinux's format.
	 * struct bpf_struct_ops_tcp_congestion_ops {
	 *	[... some other kernel fields ...]
	 *	struct tcp_congestion_ops data;
	 * }
	 * kern_vdata-size == sizeof(struct bpf_struct_ops_tcp_congestion_ops)
	 * bpf_map__init_kern_struct_ops() will populate the "kern_vdata"
	 * from "data".
	 */
	void *kern_vdata;
	__u32 type_id;
};

#define DATA_SEC ".data"
#define BSS_SEC ".bss"
#define RODATA_SEC ".rodata"
#define KCONFIG_SEC ".kconfig"
#define STRUCT_OPS_SEC ".struct_ops"

enum libbpf_map_type {
	LIBBPF_MAP_UNSPEC,
	LIBBPF_MAP_DATA,
	LIBBPF_MAP_BSS,
	LIBBPF_MAP_RODATA,
	LIBBPF_MAP_KCONFIG,
};

static const char * const libbpf_type_to_btf_name[] = {
	[LIBBPF_MAP_DATA]	= DATA_SEC,
	[LIBBPF_MAP_BSS]	= BSS_SEC,
	[LIBBPF_MAP_RODATA]	= RODATA_SEC,
	[LIBBPF_MAP_KCONFIG]	= KCONFIG_SEC,
};

struct bpf_map {
	char *name;
	int fd;
	int sec_idx;
	size_t sec_offset;
	int map_ifindex;
	int inner_map_fd;
	struct bpf_map_def def;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
	__u32 btf_vmlinux_value_type_id;
	void *priv;
	bpf_map_clear_priv_t clear_priv;
	enum libbpf_map_type libbpf_type;
	void *mmaped;
	struct bpf_struct_ops *st_ops;
	char *pin_path;
	bool pinned;
	bool reused;
};

enum extern_type {
	EXT_UNKNOWN,
	EXT_CHAR,
	EXT_BOOL,
	EXT_INT,
	EXT_TRISTATE,
	EXT_CHAR_ARR,
};

struct extern_desc {
	const char *name;
	int sym_idx;
	int btf_id;
	enum extern_type type;
	int sz;
	int align;
	int data_off;
	bool is_signed;
	bool is_weak;
	bool is_set;
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

	char *kconfig;
	struct extern_desc *externs;
	int nr_extern;
	int kconfig_map_idx;

	bool loaded;
	bool has_pseudo_calls;

	/*
	 * Information when doing elf related work. Only valid if fd
	 * is valid.
	 */
	struct {
		int fd;
		const void *obj_buf;
		size_t obj_buf_sz;
		Elf *elf;
		GElf_Ehdr ehdr;
		Elf_Data *symbols;
		Elf_Data *data;
		Elf_Data *rodata;
		Elf_Data *bss;
		Elf_Data *st_ops_data;
		size_t strtabidx;
		struct {
			GElf_Shdr shdr;
			Elf_Data *data;
		} *reloc_sects;
		int nr_reloc_sects;
		int maps_shndx;
		int btf_maps_shndx;
		int text_shndx;
		int symbols_shndx;
		int data_shndx;
		int rodata_shndx;
		int bss_shndx;
		int st_ops_shndx;
	} efile;
	/*
	 * All loaded bpf_object is linked in a list, which is
	 * hidden to caller. bpf_objects__<func> handlers deal with
	 * all objects.
	 */
	struct list_head list;

	struct btf *btf;
	/* Parse and load BTF vmlinux if any of the programs in the object need
	 * it at load time.
	 */
	struct btf *btf_vmlinux;
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
		pr_warn("Internal error: instances.nr is %d\n",
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
		pr_warn("corrupted section '%s', size: %zu\n",
			section_name, size);
		return -EINVAL;
	}

	memset(prog, 0, sizeof(*prog));

	prog->section_name = strdup(section_name);
	if (!prog->section_name) {
		pr_warn("failed to alloc name for prog under section(%d) %s\n",
			idx, section_name);
		goto errout;
	}

	prog->pin_name = __bpf_program__pin_name(prog);
	if (!prog->pin_name) {
		pr_warn("failed to alloc pin name for prog under section(%d) %s\n",
			idx, section_name);
		goto errout;
	}

	prog->insns = malloc(size);
	if (!prog->insns) {
		pr_warn("failed to alloc insns for prog under section %s\n",
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
		pr_warn("failed to alloc a new program under section '%s'\n",
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
				pr_warn("failed to get sym name string for prog %s\n",
					prog->section_name);
				return -LIBBPF_ERRNO__LIBELF;
			}
		}

		if (!name && prog->idx == obj->efile.text_shndx)
			name = ".text";

		if (!name) {
			pr_warn("failed to find sym for prog %s\n",
				prog->section_name);
			return -EINVAL;
		}

		prog->name = strdup(name);
		if (!prog->name) {
			pr_warn("failed to allocate memory for prog sym %s\n",
				name);
			return -ENOMEM;
		}
	}

	return 0;
}

static __u32 get_kernel_version(void)
{
	__u32 major, minor, patch;
	struct utsname info;

	uname(&info);
	if (sscanf(info.release, "%u.%u.%u", &major, &minor, &patch) != 3)
		return 0;
	return KERNEL_VERSION(major, minor, patch);
}

static const struct btf_member *
find_member_by_offset(const struct btf_type *t, __u32 bit_offset)
{
	struct btf_member *m;
	int i;

	for (i = 0, m = btf_members(t); i < btf_vlen(t); i++, m++) {
		if (btf_member_bit_offset(t, i) == bit_offset)
			return m;
	}

	return NULL;
}

static const struct btf_member *
find_member_by_name(const struct btf *btf, const struct btf_type *t,
		    const char *name)
{
	struct btf_member *m;
	int i;

	for (i = 0, m = btf_members(t); i < btf_vlen(t); i++, m++) {
		if (!strcmp(btf__name_by_offset(btf, m->name_off), name))
			return m;
	}

	return NULL;
}

#define STRUCT_OPS_VALUE_PREFIX "bpf_struct_ops_"
static int find_btf_by_prefix_kind(const struct btf *btf, const char *prefix,
				   const char *name, __u32 kind);

static int
find_struct_ops_kern_types(const struct btf *btf, const char *tname,
			   const struct btf_type **type, __u32 *type_id,
			   const struct btf_type **vtype, __u32 *vtype_id,
			   const struct btf_member **data_member)
{
	const struct btf_type *kern_type, *kern_vtype;
	const struct btf_member *kern_data_member;
	__s32 kern_vtype_id, kern_type_id;
	__u32 i;

	kern_type_id = btf__find_by_name_kind(btf, tname, BTF_KIND_STRUCT);
	if (kern_type_id < 0) {
		pr_warn("struct_ops init_kern: struct %s is not found in kernel BTF\n",
			tname);
		return kern_type_id;
	}
	kern_type = btf__type_by_id(btf, kern_type_id);

	/* Find the corresponding "map_value" type that will be used
	 * in map_update(BPF_MAP_TYPE_STRUCT_OPS).  For example,
	 * find "struct bpf_struct_ops_tcp_congestion_ops" from the
	 * btf_vmlinux.
	 */
	kern_vtype_id = find_btf_by_prefix_kind(btf, STRUCT_OPS_VALUE_PREFIX,
						tname, BTF_KIND_STRUCT);
	if (kern_vtype_id < 0) {
		pr_warn("struct_ops init_kern: struct %s%s is not found in kernel BTF\n",
			STRUCT_OPS_VALUE_PREFIX, tname);
		return kern_vtype_id;
	}
	kern_vtype = btf__type_by_id(btf, kern_vtype_id);

	/* Find "struct tcp_congestion_ops" from
	 * struct bpf_struct_ops_tcp_congestion_ops {
	 *	[ ... ]
	 *	struct tcp_congestion_ops data;
	 * }
	 */
	kern_data_member = btf_members(kern_vtype);
	for (i = 0; i < btf_vlen(kern_vtype); i++, kern_data_member++) {
		if (kern_data_member->type == kern_type_id)
			break;
	}
	if (i == btf_vlen(kern_vtype)) {
		pr_warn("struct_ops init_kern: struct %s data is not found in struct %s%s\n",
			tname, STRUCT_OPS_VALUE_PREFIX, tname);
		return -EINVAL;
	}

	*type = kern_type;
	*type_id = kern_type_id;
	*vtype = kern_vtype;
	*vtype_id = kern_vtype_id;
	*data_member = kern_data_member;

	return 0;
}

static bool bpf_map__is_struct_ops(const struct bpf_map *map)
{
	return map->def.type == BPF_MAP_TYPE_STRUCT_OPS;
}

/* Init the map's fields that depend on kern_btf */
static int bpf_map__init_kern_struct_ops(struct bpf_map *map,
					 const struct btf *btf,
					 const struct btf *kern_btf)
{
	const struct btf_member *member, *kern_member, *kern_data_member;
	const struct btf_type *type, *kern_type, *kern_vtype;
	__u32 i, kern_type_id, kern_vtype_id, kern_data_off;
	struct bpf_struct_ops *st_ops;
	void *data, *kern_data;
	const char *tname;
	int err;

	st_ops = map->st_ops;
	type = st_ops->type;
	tname = st_ops->tname;
	err = find_struct_ops_kern_types(kern_btf, tname,
					 &kern_type, &kern_type_id,
					 &kern_vtype, &kern_vtype_id,
					 &kern_data_member);
	if (err)
		return err;

	pr_debug("struct_ops init_kern %s: type_id:%u kern_type_id:%u kern_vtype_id:%u\n",
		 map->name, st_ops->type_id, kern_type_id, kern_vtype_id);

	map->def.value_size = kern_vtype->size;
	map->btf_vmlinux_value_type_id = kern_vtype_id;

	st_ops->kern_vdata = calloc(1, kern_vtype->size);
	if (!st_ops->kern_vdata)
		return -ENOMEM;

	data = st_ops->data;
	kern_data_off = kern_data_member->offset / 8;
	kern_data = st_ops->kern_vdata + kern_data_off;

	member = btf_members(type);
	for (i = 0; i < btf_vlen(type); i++, member++) {
		const struct btf_type *mtype, *kern_mtype;
		__u32 mtype_id, kern_mtype_id;
		void *mdata, *kern_mdata;
		__s64 msize, kern_msize;
		__u32 moff, kern_moff;
		__u32 kern_member_idx;
		const char *mname;

		mname = btf__name_by_offset(btf, member->name_off);
		kern_member = find_member_by_name(kern_btf, kern_type, mname);
		if (!kern_member) {
			pr_warn("struct_ops init_kern %s: Cannot find member %s in kernel BTF\n",
				map->name, mname);
			return -ENOTSUP;
		}

		kern_member_idx = kern_member - btf_members(kern_type);
		if (btf_member_bitfield_size(type, i) ||
		    btf_member_bitfield_size(kern_type, kern_member_idx)) {
			pr_warn("struct_ops init_kern %s: bitfield %s is not supported\n",
				map->name, mname);
			return -ENOTSUP;
		}

		moff = member->offset / 8;
		kern_moff = kern_member->offset / 8;

		mdata = data + moff;
		kern_mdata = kern_data + kern_moff;

		mtype = skip_mods_and_typedefs(btf, member->type, &mtype_id);
		kern_mtype = skip_mods_and_typedefs(kern_btf, kern_member->type,
						    &kern_mtype_id);
		if (BTF_INFO_KIND(mtype->info) !=
		    BTF_INFO_KIND(kern_mtype->info)) {
			pr_warn("struct_ops init_kern %s: Unmatched member type %s %u != %u(kernel)\n",
				map->name, mname, BTF_INFO_KIND(mtype->info),
				BTF_INFO_KIND(kern_mtype->info));
			return -ENOTSUP;
		}

		if (btf_is_ptr(mtype)) {
			struct bpf_program *prog;

			mtype = skip_mods_and_typedefs(btf, mtype->type, &mtype_id);
			kern_mtype = skip_mods_and_typedefs(kern_btf,
							    kern_mtype->type,
							    &kern_mtype_id);
			if (!btf_is_func_proto(mtype) ||
			    !btf_is_func_proto(kern_mtype)) {
				pr_warn("struct_ops init_kern %s: non func ptr %s is not supported\n",
					map->name, mname);
				return -ENOTSUP;
			}

			prog = st_ops->progs[i];
			if (!prog) {
				pr_debug("struct_ops init_kern %s: func ptr %s is not set\n",
					 map->name, mname);
				continue;
			}

			prog->attach_btf_id = kern_type_id;
			prog->expected_attach_type = kern_member_idx;

			st_ops->kern_func_off[i] = kern_data_off + kern_moff;

			pr_debug("struct_ops init_kern %s: func ptr %s is set to prog %s from data(+%u) to kern_data(+%u)\n",
				 map->name, mname, prog->name, moff,
				 kern_moff);

			continue;
		}

		msize = btf__resolve_size(btf, mtype_id);
		kern_msize = btf__resolve_size(kern_btf, kern_mtype_id);
		if (msize < 0 || kern_msize < 0 || msize != kern_msize) {
			pr_warn("struct_ops init_kern %s: Error in size of member %s: %zd != %zd(kernel)\n",
				map->name, mname, (ssize_t)msize,
				(ssize_t)kern_msize);
			return -ENOTSUP;
		}

		pr_debug("struct_ops init_kern %s: copy %s %u bytes from data(+%u) to kern_data(+%u)\n",
			 map->name, mname, (unsigned int)msize,
			 moff, kern_moff);
		memcpy(kern_mdata, mdata, msize);
	}

	return 0;
}

static int bpf_object__init_kern_struct_ops_maps(struct bpf_object *obj)
{
	struct bpf_map *map;
	size_t i;
	int err;

	for (i = 0; i < obj->nr_maps; i++) {
		map = &obj->maps[i];

		if (!bpf_map__is_struct_ops(map))
			continue;

		err = bpf_map__init_kern_struct_ops(map, obj->btf,
						    obj->btf_vmlinux);
		if (err)
			return err;
	}

	return 0;
}

static int bpf_object__init_struct_ops_maps(struct bpf_object *obj)
{
	const struct btf_type *type, *datasec;
	const struct btf_var_secinfo *vsi;
	struct bpf_struct_ops *st_ops;
	const char *tname, *var_name;
	__s32 type_id, datasec_id;
	const struct btf *btf;
	struct bpf_map *map;
	__u32 i;

	if (obj->efile.st_ops_shndx == -1)
		return 0;

	btf = obj->btf;
	datasec_id = btf__find_by_name_kind(btf, STRUCT_OPS_SEC,
					    BTF_KIND_DATASEC);
	if (datasec_id < 0) {
		pr_warn("struct_ops init: DATASEC %s not found\n",
			STRUCT_OPS_SEC);
		return -EINVAL;
	}

	datasec = btf__type_by_id(btf, datasec_id);
	vsi = btf_var_secinfos(datasec);
	for (i = 0; i < btf_vlen(datasec); i++, vsi++) {
		type = btf__type_by_id(obj->btf, vsi->type);
		var_name = btf__name_by_offset(obj->btf, type->name_off);

		type_id = btf__resolve_type(obj->btf, vsi->type);
		if (type_id < 0) {
			pr_warn("struct_ops init: Cannot resolve var type_id %u in DATASEC %s\n",
				vsi->type, STRUCT_OPS_SEC);
			return -EINVAL;
		}

		type = btf__type_by_id(obj->btf, type_id);
		tname = btf__name_by_offset(obj->btf, type->name_off);
		if (!tname[0]) {
			pr_warn("struct_ops init: anonymous type is not supported\n");
			return -ENOTSUP;
		}
		if (!btf_is_struct(type)) {
			pr_warn("struct_ops init: %s is not a struct\n", tname);
			return -EINVAL;
		}

		map = bpf_object__add_map(obj);
		if (IS_ERR(map))
			return PTR_ERR(map);

		map->sec_idx = obj->efile.st_ops_shndx;
		map->sec_offset = vsi->offset;
		map->name = strdup(var_name);
		if (!map->name)
			return -ENOMEM;

		map->def.type = BPF_MAP_TYPE_STRUCT_OPS;
		map->def.key_size = sizeof(int);
		map->def.value_size = type->size;
		map->def.max_entries = 1;

		map->st_ops = calloc(1, sizeof(*map->st_ops));
		if (!map->st_ops)
			return -ENOMEM;
		st_ops = map->st_ops;
		st_ops->data = malloc(type->size);
		st_ops->progs = calloc(btf_vlen(type), sizeof(*st_ops->progs));
		st_ops->kern_func_off = malloc(btf_vlen(type) *
					       sizeof(*st_ops->kern_func_off));
		if (!st_ops->data || !st_ops->progs || !st_ops->kern_func_off)
			return -ENOMEM;

		if (vsi->offset + type->size > obj->efile.st_ops_data->d_size) {
			pr_warn("struct_ops init: var %s is beyond the end of DATASEC %s\n",
				var_name, STRUCT_OPS_SEC);
			return -EINVAL;
		}

		memcpy(st_ops->data,
		       obj->efile.st_ops_data->d_buf + vsi->offset,
		       type->size);
		st_ops->tname = tname;
		st_ops->type = type;
		st_ops->type_id = type_id;

		pr_debug("struct_ops init: struct %s(type_id=%u) %s found at offset %u\n",
			 tname, type_id, var_name, vsi->offset);
	}

	return 0;
}

static struct bpf_object *bpf_object__new(const char *path,
					  const void *obj_buf,
					  size_t obj_buf_sz,
					  const char *obj_name)
{
	struct bpf_object *obj;
	char *end;

	obj = calloc(1, sizeof(struct bpf_object) + strlen(path) + 1);
	if (!obj) {
		pr_warn("alloc memory failed for %s\n", path);
		return ERR_PTR(-ENOMEM);
	}

	strcpy(obj->path, path);
	if (obj_name) {
		strncpy(obj->name, obj_name, sizeof(obj->name) - 1);
		obj->name[sizeof(obj->name) - 1] = 0;
	} else {
		/* Using basename() GNU version which doesn't modify arg. */
		strncpy(obj->name, basename((void *)path),
			sizeof(obj->name) - 1);
		end = strchr(obj->name, '.');
		if (end)
			*end = 0;
	}

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
	obj->efile.st_ops_shndx = -1;
	obj->kconfig_map_idx = -1;

	obj->kern_version = get_kernel_version();
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
	obj->efile.st_ops_data = NULL;

	zfree(&obj->efile.reloc_sects);
	obj->efile.nr_reloc_sects = 0;
	zclose(obj->efile.fd);
	obj->efile.obj_buf = NULL;
	obj->efile.obj_buf_sz = 0;
}

static int bpf_object__elf_init(struct bpf_object *obj)
{
	int err = 0;
	GElf_Ehdr *ep;

	if (obj_elf_valid(obj)) {
		pr_warn("elf init: internal error\n");
		return -LIBBPF_ERRNO__LIBELF;
	}

	if (obj->efile.obj_buf_sz > 0) {
		/*
		 * obj_buf should have been validated by
		 * bpf_object__open_buffer().
		 */
		obj->efile.elf = elf_memory((char *)obj->efile.obj_buf,
					    obj->efile.obj_buf_sz);
	} else {
		obj->efile.fd = open(obj->path, O_RDONLY);
		if (obj->efile.fd < 0) {
			char errmsg[STRERR_BUFSIZE], *cp;

			err = -errno;
			cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
			pr_warn("failed to open %s: %s\n", obj->path, cp);
			return err;
		}

		obj->efile.elf = elf_begin(obj->efile.fd,
					   LIBBPF_ELF_C_READ_MMAP, NULL);
	}

	if (!obj->efile.elf) {
		pr_warn("failed to open %s as ELF file\n", obj->path);
		err = -LIBBPF_ERRNO__LIBELF;
		goto errout;
	}

	if (!gelf_getehdr(obj->efile.elf, &obj->efile.ehdr)) {
		pr_warn("failed to get EHDR from %s\n", obj->path);
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}
	ep = &obj->efile.ehdr;

	/* Old LLVM set e_machine to EM_NONE */
	if (ep->e_type != ET_REL ||
	    (ep->e_machine && ep->e_machine != EM_BPF)) {
		pr_warn("%s is not an eBPF object file\n", obj->path);
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
	pr_warn("endianness mismatch.\n");
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
		pr_warn("invalid kver section in %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}
	memcpy(&kver, data, sizeof(kver));
	obj->kern_version = kver;
	pr_debug("kernel version of %s is %x\n", obj->path, obj->kern_version);
	return 0;
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
			pr_warn("failed to get section(%d) header from %s\n",
				idx, obj->path);
			return -EIO;
		}

		sec_name = elf_strptr(elf, ep->e_shstrndx, sh.sh_name);
		if (!sec_name) {
			pr_warn("failed to get section(%d) name from %s\n",
				idx, obj->path);
			return -EIO;
		}

		if (strcmp(name, sec_name))
			continue;

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warn("failed to get section(%d) data from %s(%s)\n",
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
	} else if (!strcmp(name, DATA_SEC)) {
		if (obj->efile.data)
			*size = obj->efile.data->d_size;
	} else if (!strcmp(name, BSS_SEC)) {
		if (obj->efile.bss)
			*size = obj->efile.bss->d_size;
	} else if (!strcmp(name, RODATA_SEC)) {
		if (obj->efile.rodata)
			*size = obj->efile.rodata->d_size;
	} else if (!strcmp(name, STRUCT_OPS_SEC)) {
		if (obj->efile.st_ops_data)
			*size = obj->efile.st_ops_data->d_size;
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
			pr_warn("failed to get sym name string for var %s\n",
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
		pr_warn("alloc maps for object failed\n");
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

static size_t bpf_map_mmap_sz(const struct bpf_map *map)
{
	long page_sz = sysconf(_SC_PAGE_SIZE);
	size_t map_sz;

	map_sz = (size_t)roundup(map->def.value_size, 8) * map->def.max_entries;
	map_sz = roundup(map_sz, page_sz);
	return map_sz;
}

static char *internal_map_name(struct bpf_object *obj,
			       enum libbpf_map_type type)
{
	char map_name[BPF_OBJ_NAME_LEN], *p;
	const char *sfx = libbpf_type_to_btf_name[type];
	int sfx_len = max((size_t)7, strlen(sfx));
	int pfx_len = min((size_t)BPF_OBJ_NAME_LEN - sfx_len - 1,
			  strlen(obj->name));

	snprintf(map_name, sizeof(map_name), "%.*s%.*s", pfx_len, obj->name,
		 sfx_len, libbpf_type_to_btf_name[type]);

	/* sanitise map name to characters allowed by kernel */
	for (p = map_name; *p && p < map_name + sizeof(map_name); p++)
		if (!isalnum(*p) && *p != '_' && *p != '.')
			*p = '_';

	return strdup(map_name);
}

static int
bpf_object__init_internal_map(struct bpf_object *obj, enum libbpf_map_type type,
			      int sec_idx, void *data, size_t data_sz)
{
	struct bpf_map_def *def;
	struct bpf_map *map;
	int err;

	map = bpf_object__add_map(obj);
	if (IS_ERR(map))
		return PTR_ERR(map);

	map->libbpf_type = type;
	map->sec_idx = sec_idx;
	map->sec_offset = 0;
	map->name = internal_map_name(obj, type);
	if (!map->name) {
		pr_warn("failed to alloc map name\n");
		return -ENOMEM;
	}

	def = &map->def;
	def->type = BPF_MAP_TYPE_ARRAY;
	def->key_size = sizeof(int);
	def->value_size = data_sz;
	def->max_entries = 1;
	def->map_flags = type == LIBBPF_MAP_RODATA || type == LIBBPF_MAP_KCONFIG
			 ? BPF_F_RDONLY_PROG : 0;
	def->map_flags |= BPF_F_MMAPABLE;

	pr_debug("map '%s' (global data): at sec_idx %d, offset %zu, flags %x.\n",
		 map->name, map->sec_idx, map->sec_offset, def->map_flags);

	map->mmaped = mmap(NULL, bpf_map_mmap_sz(map), PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (map->mmaped == MAP_FAILED) {
		err = -errno;
		map->mmaped = NULL;
		pr_warn("failed to alloc map '%s' content buffer: %d\n",
			map->name, err);
		zfree(&map->name);
		return err;
	}

	if (data)
		memcpy(map->mmaped, data, data_sz);

	pr_debug("map %td is \"%s\"\n", map - obj->maps, map->name);
	return 0;
}

static int bpf_object__init_global_data_maps(struct bpf_object *obj)
{
	int err;

	/*
	 * Populate obj->maps with libbpf internal maps.
	 */
	if (obj->efile.data_shndx >= 0) {
		err = bpf_object__init_internal_map(obj, LIBBPF_MAP_DATA,
						    obj->efile.data_shndx,
						    obj->efile.data->d_buf,
						    obj->efile.data->d_size);
		if (err)
			return err;
	}
	if (obj->efile.rodata_shndx >= 0) {
		err = bpf_object__init_internal_map(obj, LIBBPF_MAP_RODATA,
						    obj->efile.rodata_shndx,
						    obj->efile.rodata->d_buf,
						    obj->efile.rodata->d_size);
		if (err)
			return err;
	}
	if (obj->efile.bss_shndx >= 0) {
		err = bpf_object__init_internal_map(obj, LIBBPF_MAP_BSS,
						    obj->efile.bss_shndx,
						    NULL,
						    obj->efile.bss->d_size);
		if (err)
			return err;
	}
	return 0;
}


static struct extern_desc *find_extern_by_name(const struct bpf_object *obj,
					       const void *name)
{
	int i;

	for (i = 0; i < obj->nr_extern; i++) {
		if (strcmp(obj->externs[i].name, name) == 0)
			return &obj->externs[i];
	}
	return NULL;
}

static int set_ext_value_tri(struct extern_desc *ext, void *ext_val,
			     char value)
{
	switch (ext->type) {
	case EXT_BOOL:
		if (value == 'm') {
			pr_warn("extern %s=%c should be tristate or char\n",
				ext->name, value);
			return -EINVAL;
		}
		*(bool *)ext_val = value == 'y' ? true : false;
		break;
	case EXT_TRISTATE:
		if (value == 'y')
			*(enum libbpf_tristate *)ext_val = TRI_YES;
		else if (value == 'm')
			*(enum libbpf_tristate *)ext_val = TRI_MODULE;
		else /* value == 'n' */
			*(enum libbpf_tristate *)ext_val = TRI_NO;
		break;
	case EXT_CHAR:
		*(char *)ext_val = value;
		break;
	case EXT_UNKNOWN:
	case EXT_INT:
	case EXT_CHAR_ARR:
	default:
		pr_warn("extern %s=%c should be bool, tristate, or char\n",
			ext->name, value);
		return -EINVAL;
	}
	ext->is_set = true;
	return 0;
}

static int set_ext_value_str(struct extern_desc *ext, char *ext_val,
			     const char *value)
{
	size_t len;

	if (ext->type != EXT_CHAR_ARR) {
		pr_warn("extern %s=%s should char array\n", ext->name, value);
		return -EINVAL;
	}

	len = strlen(value);
	if (value[len - 1] != '"') {
		pr_warn("extern '%s': invalid string config '%s'\n",
			ext->name, value);
		return -EINVAL;
	}

	/* strip quotes */
	len -= 2;
	if (len >= ext->sz) {
		pr_warn("extern '%s': long string config %s of (%zu bytes) truncated to %d bytes\n",
			ext->name, value, len, ext->sz - 1);
		len = ext->sz - 1;
	}
	memcpy(ext_val, value + 1, len);
	ext_val[len] = '\0';
	ext->is_set = true;
	return 0;
}

static int parse_u64(const char *value, __u64 *res)
{
	char *value_end;
	int err;

	errno = 0;
	*res = strtoull(value, &value_end, 0);
	if (errno) {
		err = -errno;
		pr_warn("failed to parse '%s' as integer: %d\n", value, err);
		return err;
	}
	if (*value_end) {
		pr_warn("failed to parse '%s' as integer completely\n", value);
		return -EINVAL;
	}
	return 0;
}

static bool is_ext_value_in_range(const struct extern_desc *ext, __u64 v)
{
	int bit_sz = ext->sz * 8;

	if (ext->sz == 8)
		return true;

	/* Validate that value stored in u64 fits in integer of `ext->sz`
	 * bytes size without any loss of information. If the target integer
	 * is signed, we rely on the following limits of integer type of
	 * Y bits and subsequent transformation:
	 *
	 *     -2^(Y-1) <= X           <= 2^(Y-1) - 1
	 *            0 <= X + 2^(Y-1) <= 2^Y - 1
	 *            0 <= X + 2^(Y-1) <  2^Y
	 *
	 *  For unsigned target integer, check that all the (64 - Y) bits are
	 *  zero.
	 */
	if (ext->is_signed)
		return v + (1ULL << (bit_sz - 1)) < (1ULL << bit_sz);
	else
		return (v >> bit_sz) == 0;
}

static int set_ext_value_num(struct extern_desc *ext, void *ext_val,
			     __u64 value)
{
	if (ext->type != EXT_INT && ext->type != EXT_CHAR) {
		pr_warn("extern %s=%llu should be integer\n",
			ext->name, (unsigned long long)value);
		return -EINVAL;
	}
	if (!is_ext_value_in_range(ext, value)) {
		pr_warn("extern %s=%llu value doesn't fit in %d bytes\n",
			ext->name, (unsigned long long)value, ext->sz);
		return -ERANGE;
	}
	switch (ext->sz) {
		case 1: *(__u8 *)ext_val = value; break;
		case 2: *(__u16 *)ext_val = value; break;
		case 4: *(__u32 *)ext_val = value; break;
		case 8: *(__u64 *)ext_val = value; break;
		default:
			return -EINVAL;
	}
	ext->is_set = true;
	return 0;
}

static int bpf_object__process_kconfig_line(struct bpf_object *obj,
					    char *buf, void *data)
{
	struct extern_desc *ext;
	char *sep, *value;
	int len, err = 0;
	void *ext_val;
	__u64 num;

	if (strncmp(buf, "CONFIG_", 7))
		return 0;

	sep = strchr(buf, '=');
	if (!sep) {
		pr_warn("failed to parse '%s': no separator\n", buf);
		return -EINVAL;
	}

	/* Trim ending '\n' */
	len = strlen(buf);
	if (buf[len - 1] == '\n')
		buf[len - 1] = '\0';
	/* Split on '=' and ensure that a value is present. */
	*sep = '\0';
	if (!sep[1]) {
		*sep = '=';
		pr_warn("failed to parse '%s': no value\n", buf);
		return -EINVAL;
	}

	ext = find_extern_by_name(obj, buf);
	if (!ext || ext->is_set)
		return 0;

	ext_val = data + ext->data_off;
	value = sep + 1;

	switch (*value) {
	case 'y': case 'n': case 'm':
		err = set_ext_value_tri(ext, ext_val, *value);
		break;
	case '"':
		err = set_ext_value_str(ext, ext_val, value);
		break;
	default:
		/* assume integer */
		err = parse_u64(value, &num);
		if (err) {
			pr_warn("extern %s=%s should be integer\n",
				ext->name, value);
			return err;
		}
		err = set_ext_value_num(ext, ext_val, num);
		break;
	}
	if (err)
		return err;
	pr_debug("extern %s=%s\n", ext->name, value);
	return 0;
}

static int bpf_object__read_kconfig_file(struct bpf_object *obj, void *data)
{
	char buf[PATH_MAX];
	struct utsname uts;
	int len, err = 0;
	gzFile file;

	uname(&uts);
	len = snprintf(buf, PATH_MAX, "/boot/config-%s", uts.release);
	if (len < 0)
		return -EINVAL;
	else if (len >= PATH_MAX)
		return -ENAMETOOLONG;

	/* gzopen also accepts uncompressed files. */
	file = gzopen(buf, "r");
	if (!file)
		file = gzopen("/proc/config.gz", "r");

	if (!file) {
		pr_warn("failed to open system Kconfig\n");
		return -ENOENT;
	}

	while (gzgets(file, buf, sizeof(buf))) {
		err = bpf_object__process_kconfig_line(obj, buf, data);
		if (err) {
			pr_warn("error parsing system Kconfig line '%s': %d\n",
				buf, err);
			goto out;
		}
	}

out:
	gzclose(file);
	return err;
}

static int bpf_object__read_kconfig_mem(struct bpf_object *obj,
					const char *config, void *data)
{
	char buf[PATH_MAX];
	int err = 0;
	FILE *file;

	file = fmemopen((void *)config, strlen(config), "r");
	if (!file) {
		err = -errno;
		pr_warn("failed to open in-memory Kconfig: %d\n", err);
		return err;
	}

	while (fgets(buf, sizeof(buf), file)) {
		err = bpf_object__process_kconfig_line(obj, buf, data);
		if (err) {
			pr_warn("error parsing in-memory Kconfig line '%s': %d\n",
				buf, err);
			break;
		}
	}

	fclose(file);
	return err;
}

static int bpf_object__init_kconfig_map(struct bpf_object *obj)
{
	struct extern_desc *last_ext;
	size_t map_sz;
	int err;

	if (obj->nr_extern == 0)
		return 0;

	last_ext = &obj->externs[obj->nr_extern - 1];
	map_sz = last_ext->data_off + last_ext->sz;

	err = bpf_object__init_internal_map(obj, LIBBPF_MAP_KCONFIG,
					    obj->efile.symbols_shndx,
					    NULL, map_sz);
	if (err)
		return err;

	obj->kconfig_map_idx = obj->nr_maps - 1;

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
		pr_warn("failed to get Elf_Data from map section %d\n",
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

	if (!data->d_size || nr_maps == 0 || (data->d_size % nr_maps) != 0) {
		pr_warn("unable to determine map definition size section %s, %d maps in %zd bytes\n",
			obj->path, nr_maps, data->d_size);
		return -EINVAL;
	}
	map_def_sz = data->d_size / nr_maps;

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
			pr_warn("failed to get map #%d name sym string for obj %s\n",
				i, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		map->libbpf_type = LIBBPF_MAP_UNSPEC;
		map->sec_idx = sym.st_shndx;
		map->sec_offset = sym.st_value;
		pr_debug("map '%s' (legacy): at sec_idx %d, offset %zu.\n",
			 map_name, map->sec_idx, map->sec_offset);
		if (sym.st_value + map_def_sz > data->d_size) {
			pr_warn("corrupted maps section in %s: last map \"%s\" too small\n",
				obj->path, map_name);
			return -EINVAL;
		}

		map->name = strdup(map_name);
		if (!map->name) {
			pr_warn("failed to alloc map name\n");
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
					pr_warn("maps section in %s: \"%s\" has unrecognized, non-zero options\n",
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

static const struct btf_type *
skip_mods_and_typedefs(const struct btf *btf, __u32 id, __u32 *res_id)
{
	const struct btf_type *t = btf__type_by_id(btf, id);

	if (res_id)
		*res_id = id;

	while (btf_is_mod(t) || btf_is_typedef(t)) {
		if (res_id)
			*res_id = t->type;
		t = btf__type_by_id(btf, t->type);
	}

	return t;
}

static const struct btf_type *
resolve_func_ptr(const struct btf *btf, __u32 id, __u32 *res_id)
{
	const struct btf_type *t;

	t = skip_mods_and_typedefs(btf, id, NULL);
	if (!btf_is_ptr(t))
		return NULL;

	t = skip_mods_and_typedefs(btf, t->type, res_id);

	return btf_is_func_proto(t) ? t : NULL;
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
			      const struct btf_member *m, __u32 *res)
{
	const struct btf_type *t = skip_mods_and_typedefs(btf, m->type, NULL);
	const char *name = btf__name_by_offset(btf, m->name_off);
	const struct btf_array *arr_info;
	const struct btf_type *arr_t;

	if (!btf_is_ptr(t)) {
		pr_warn("map '%s': attr '%s': expected PTR, got %u.\n",
			map_name, name, btf_kind(t));
		return false;
	}

	arr_t = btf__type_by_id(btf, t->type);
	if (!arr_t) {
		pr_warn("map '%s': attr '%s': type [%u] not found.\n",
			map_name, name, t->type);
		return false;
	}
	if (!btf_is_array(arr_t)) {
		pr_warn("map '%s': attr '%s': expected ARRAY, got %u.\n",
			map_name, name, btf_kind(arr_t));
		return false;
	}
	arr_info = btf_array(arr_t);
	*res = arr_info->nelems;
	return true;
}

static int build_map_pin_path(struct bpf_map *map, const char *path)
{
	char buf[PATH_MAX];
	int err, len;

	if (!path)
		path = "/sys/fs/bpf";

	len = snprintf(buf, PATH_MAX, "%s/%s", path, bpf_map__name(map));
	if (len < 0)
		return -EINVAL;
	else if (len >= PATH_MAX)
		return -ENAMETOOLONG;

	err = bpf_map__set_pin_path(map, buf);
	if (err)
		return err;

	return 0;
}

static int bpf_object__init_user_btf_map(struct bpf_object *obj,
					 const struct btf_type *sec,
					 int var_idx, int sec_idx,
					 const Elf_Data *data, bool strict,
					 const char *pin_root_path)
{
	const struct btf_type *var, *def, *t;
	const struct btf_var_secinfo *vi;
	const struct btf_var *var_extra;
	const struct btf_member *m;
	const char *map_name;
	struct bpf_map *map;
	int vlen, i;

	vi = btf_var_secinfos(sec) + var_idx;
	var = btf__type_by_id(obj->btf, vi->type);
	var_extra = btf_var(var);
	map_name = btf__name_by_offset(obj->btf, var->name_off);
	vlen = btf_vlen(var);

	if (map_name == NULL || map_name[0] == '\0') {
		pr_warn("map #%d: empty name.\n", var_idx);
		return -EINVAL;
	}
	if ((__u64)vi->offset + vi->size > data->d_size) {
		pr_warn("map '%s' BTF data is corrupted.\n", map_name);
		return -EINVAL;
	}
	if (!btf_is_var(var)) {
		pr_warn("map '%s': unexpected var kind %u.\n",
			map_name, btf_kind(var));
		return -EINVAL;
	}
	if (var_extra->linkage != BTF_VAR_GLOBAL_ALLOCATED &&
	    var_extra->linkage != BTF_VAR_STATIC) {
		pr_warn("map '%s': unsupported var linkage %u.\n",
			map_name, var_extra->linkage);
		return -EOPNOTSUPP;
	}

	def = skip_mods_and_typedefs(obj->btf, var->type, NULL);
	if (!btf_is_struct(def)) {
		pr_warn("map '%s': unexpected def kind %u.\n",
			map_name, btf_kind(var));
		return -EINVAL;
	}
	if (def->size > vi->size) {
		pr_warn("map '%s': invalid def size.\n", map_name);
		return -EINVAL;
	}

	map = bpf_object__add_map(obj);
	if (IS_ERR(map))
		return PTR_ERR(map);
	map->name = strdup(map_name);
	if (!map->name) {
		pr_warn("map '%s': failed to alloc map name.\n", map_name);
		return -ENOMEM;
	}
	map->libbpf_type = LIBBPF_MAP_UNSPEC;
	map->def.type = BPF_MAP_TYPE_UNSPEC;
	map->sec_idx = sec_idx;
	map->sec_offset = vi->offset;
	pr_debug("map '%s': at sec_idx %d, offset %zu.\n",
		 map_name, map->sec_idx, map->sec_offset);

	vlen = btf_vlen(def);
	m = btf_members(def);
	for (i = 0; i < vlen; i++, m++) {
		const char *name = btf__name_by_offset(obj->btf, m->name_off);

		if (!name) {
			pr_warn("map '%s': invalid field #%d.\n", map_name, i);
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
				pr_warn("map '%s': conflicting key size %u != %u.\n",
					map_name, map->def.key_size, sz);
				return -EINVAL;
			}
			map->def.key_size = sz;
		} else if (strcmp(name, "key") == 0) {
			__s64 sz;

			t = btf__type_by_id(obj->btf, m->type);
			if (!t) {
				pr_warn("map '%s': key type [%d] not found.\n",
					map_name, m->type);
				return -EINVAL;
			}
			if (!btf_is_ptr(t)) {
				pr_warn("map '%s': key spec is not PTR: %u.\n",
					map_name, btf_kind(t));
				return -EINVAL;
			}
			sz = btf__resolve_size(obj->btf, t->type);
			if (sz < 0) {
				pr_warn("map '%s': can't determine key size for type [%u]: %zd.\n",
					map_name, t->type, (ssize_t)sz);
				return sz;
			}
			pr_debug("map '%s': found key [%u], sz = %zd.\n",
				 map_name, t->type, (ssize_t)sz);
			if (map->def.key_size && map->def.key_size != sz) {
				pr_warn("map '%s': conflicting key size %u != %zd.\n",
					map_name, map->def.key_size, (ssize_t)sz);
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
				pr_warn("map '%s': conflicting value size %u != %u.\n",
					map_name, map->def.value_size, sz);
				return -EINVAL;
			}
			map->def.value_size = sz;
		} else if (strcmp(name, "value") == 0) {
			__s64 sz;

			t = btf__type_by_id(obj->btf, m->type);
			if (!t) {
				pr_warn("map '%s': value type [%d] not found.\n",
					map_name, m->type);
				return -EINVAL;
			}
			if (!btf_is_ptr(t)) {
				pr_warn("map '%s': value spec is not PTR: %u.\n",
					map_name, btf_kind(t));
				return -EINVAL;
			}
			sz = btf__resolve_size(obj->btf, t->type);
			if (sz < 0) {
				pr_warn("map '%s': can't determine value size for type [%u]: %zd.\n",
					map_name, t->type, (ssize_t)sz);
				return sz;
			}
			pr_debug("map '%s': found value [%u], sz = %zd.\n",
				 map_name, t->type, (ssize_t)sz);
			if (map->def.value_size && map->def.value_size != sz) {
				pr_warn("map '%s': conflicting value size %u != %zd.\n",
					map_name, map->def.value_size, (ssize_t)sz);
				return -EINVAL;
			}
			map->def.value_size = sz;
			map->btf_value_type_id = t->type;
		} else if (strcmp(name, "pinning") == 0) {
			__u32 val;
			int err;

			if (!get_map_field_int(map_name, obj->btf, def, m,
					       &val))
				return -EINVAL;
			pr_debug("map '%s': found pinning = %u.\n",
				 map_name, val);

			if (val != LIBBPF_PIN_NONE &&
			    val != LIBBPF_PIN_BY_NAME) {
				pr_warn("map '%s': invalid pinning value %u.\n",
					map_name, val);
				return -EINVAL;
			}
			if (val == LIBBPF_PIN_BY_NAME) {
				err = build_map_pin_path(map, pin_root_path);
				if (err) {
					pr_warn("map '%s': couldn't build pin path.\n",
						map_name);
					return err;
				}
			}
		} else {
			if (strict) {
				pr_warn("map '%s': unknown field '%s'.\n",
					map_name, name);
				return -ENOTSUP;
			}
			pr_debug("map '%s': ignoring unknown field '%s'.\n",
				 map_name, name);
		}
	}

	if (map->def.type == BPF_MAP_TYPE_UNSPEC) {
		pr_warn("map '%s': map type isn't specified.\n", map_name);
		return -EINVAL;
	}

	return 0;
}

static int bpf_object__init_user_btf_maps(struct bpf_object *obj, bool strict,
					  const char *pin_root_path)
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
		pr_warn("failed to get Elf_Data from map section %d (%s)\n",
			obj->efile.maps_shndx, MAPS_ELF_SEC);
		return -EINVAL;
	}

	nr_types = btf__get_nr_types(obj->btf);
	for (i = 1; i <= nr_types; i++) {
		t = btf__type_by_id(obj->btf, i);
		if (!btf_is_datasec(t))
			continue;
		name = btf__name_by_offset(obj->btf, t->name_off);
		if (strcmp(name, MAPS_ELF_SEC) == 0) {
			sec = t;
			break;
		}
	}

	if (!sec) {
		pr_warn("DATASEC '%s' not found.\n", MAPS_ELF_SEC);
		return -ENOENT;
	}

	vlen = btf_vlen(sec);
	for (i = 0; i < vlen; i++) {
		err = bpf_object__init_user_btf_map(obj, sec, i,
						    obj->efile.btf_maps_shndx,
						    data, strict,
						    pin_root_path);
		if (err)
			return err;
	}

	return 0;
}

static int bpf_object__init_maps(struct bpf_object *obj,
				 const struct bpf_object_open_opts *opts)
{
	const char *pin_root_path;
	bool strict;
	int err;

	strict = !OPTS_GET(opts, relaxed_maps, false);
	pin_root_path = OPTS_GET(opts, pin_root_path, NULL);

	err = bpf_object__init_user_maps(obj, strict);
	err = err ?: bpf_object__init_user_btf_maps(obj, strict, pin_root_path);
	err = err ?: bpf_object__init_global_data_maps(obj);
	err = err ?: bpf_object__init_kconfig_map(obj);
	err = err ?: bpf_object__init_struct_ops_maps(obj);
	if (err)
		return err;

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
	bool has_func_global = obj->caps.btf_func_global;
	bool has_datasec = obj->caps.btf_datasec;
	bool has_func = obj->caps.btf_func;
	struct btf *btf = obj->btf;
	struct btf_type *t;
	int i, j, vlen;

	if (!obj->btf || (has_func && has_datasec && has_func_global))
		return;

	for (i = 1; i <= btf__get_nr_types(btf); i++) {
		t = (struct btf_type *)btf__type_by_id(btf, i);

		if (!has_datasec && btf_is_var(t)) {
			/* replace VAR with INT */
			t->info = BTF_INFO_ENC(BTF_KIND_INT, 0, 0);
			/*
			 * using size = 1 is the safest choice, 4 will be too
			 * big and cause kernel BTF validation failure if
			 * original variable took less than 4 bytes
			 */
			t->size = 1;
			*(int *)(t + 1) = BTF_INT_ENC(0, 0, 8);
		} else if (!has_datasec && btf_is_datasec(t)) {
			/* replace DATASEC with STRUCT */
			const struct btf_var_secinfo *v = btf_var_secinfos(t);
			struct btf_member *m = btf_members(t);
			struct btf_type *vt;
			char *name;

			name = (char *)btf__name_by_offset(btf, t->name_off);
			while (*name) {
				if (*name == '.')
					*name = '_';
				name++;
			}

			vlen = btf_vlen(t);
			t->info = BTF_INFO_ENC(BTF_KIND_STRUCT, 0, vlen);
			for (j = 0; j < vlen; j++, v++, m++) {
				/* order of field assignments is important */
				m->offset = v->offset * 8;
				m->type = v->type;
				/* preserve variable name as member name */
				vt = (void *)btf__type_by_id(btf, v->type);
				m->name_off = vt->name_off;
			}
		} else if (!has_func && btf_is_func_proto(t)) {
			/* replace FUNC_PROTO with ENUM */
			vlen = btf_vlen(t);
			t->info = BTF_INFO_ENC(BTF_KIND_ENUM, 0, vlen);
			t->size = sizeof(__u32); /* kernel enforced */
		} else if (!has_func && btf_is_func(t)) {
			/* replace FUNC with TYPEDEF */
			t->info = BTF_INFO_ENC(BTF_KIND_TYPEDEF, 0, 0);
		} else if (!has_func_global && btf_is_func(t)) {
			/* replace BTF_FUNC_GLOBAL with BTF_FUNC_STATIC */
			t->info = BTF_INFO_ENC(BTF_KIND_FUNC, 0, 0);
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
	return obj->efile.st_ops_shndx >= 0 || obj->nr_extern > 0;
}

static int bpf_object__init_btf(struct bpf_object *obj,
				Elf_Data *btf_data,
				Elf_Data *btf_ext_data)
{
	int err = -ENOENT;

	if (btf_data) {
		obj->btf = btf__new(btf_data->d_buf, btf_data->d_size);
		if (IS_ERR(obj->btf)) {
			err = PTR_ERR(obj->btf);
			obj->btf = NULL;
			pr_warn("Error loading ELF section %s: %d.\n",
				BTF_ELF_SEC, err);
			goto out;
		}
		err = 0;
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
			pr_warn("Error loading ELF section %s: %ld. Ignored and continue.\n",
				BTF_EXT_ELF_SEC, PTR_ERR(obj->btf_ext));
			obj->btf_ext = NULL;
			goto out;
		}
	}
out:
	if (err && bpf_object__is_btf_mandatory(obj)) {
		pr_warn("BTF is required, but is missing or corrupted.\n");
		return err;
	}
	return 0;
}

static int bpf_object__finalize_btf(struct bpf_object *obj)
{
	int err;

	if (!obj->btf)
		return 0;

	err = btf__finalize_data(obj, obj->btf);
	if (!err)
		return 0;

	pr_warn("Error finalizing %s: %d.\n", BTF_ELF_SEC, err);
	btf__free(obj->btf);
	obj->btf = NULL;
	btf_ext__free(obj->btf_ext);
	obj->btf_ext = NULL;

	if (bpf_object__is_btf_mandatory(obj)) {
		pr_warn("BTF is required, but is missing or corrupted.\n");
		return -ENOENT;
	}
	return 0;
}

static inline bool libbpf_prog_needs_vmlinux_btf(struct bpf_program *prog)
{
	if (prog->type == BPF_PROG_TYPE_STRUCT_OPS)
		return true;

	/* BPF_PROG_TYPE_TRACING programs which do not attach to other programs
	 * also need vmlinux BTF
	 */
	if (prog->type == BPF_PROG_TYPE_TRACING && !prog->attach_prog_fd)
		return true;

	return false;
}

static int bpf_object__load_vmlinux_btf(struct bpf_object *obj)
{
	struct bpf_program *prog;
	int err;

	bpf_object__for_each_program(prog, obj) {
		if (libbpf_prog_needs_vmlinux_btf(prog)) {
			obj->btf_vmlinux = libbpf_find_kernel_btf();
			if (IS_ERR(obj->btf_vmlinux)) {
				err = PTR_ERR(obj->btf_vmlinux);
				pr_warn("Error loading vmlinux BTF: %d\n", err);
				obj->btf_vmlinux = NULL;
				return err;
			}
			return 0;
		}
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
		pr_warn("Error loading %s into kernel: %d.\n",
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

static int bpf_object__elf_collect(struct bpf_object *obj)
{
	Elf *elf = obj->efile.elf;
	GElf_Ehdr *ep = &obj->efile.ehdr;
	Elf_Data *btf_ext_data = NULL;
	Elf_Data *btf_data = NULL;
	Elf_Scn *scn = NULL;
	int idx = 0, err = 0;

	/* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, ep->e_shstrndx), NULL)) {
		pr_warn("failed to get e_shstrndx from %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		char *name;
		GElf_Shdr sh;
		Elf_Data *data;

		idx++;
		if (gelf_getshdr(scn, &sh) != &sh) {
			pr_warn("failed to get section(%d) header from %s\n",
				idx, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		name = elf_strptr(elf, ep->e_shstrndx, sh.sh_name);
		if (!name) {
			pr_warn("failed to get section(%d) name from %s\n",
				idx, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		data = elf_getdata(scn, 0);
		if (!data) {
			pr_warn("failed to get section(%d) data from %s(%s)\n",
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
				pr_warn("bpf: multiple SYMTAB in %s\n",
					obj->path);
				return -LIBBPF_ERRNO__FORMAT;
			}
			obj->efile.symbols = data;
			obj->efile.symbols_shndx = idx;
			obj->efile.strtabidx = sh.sh_link;
		} else if (sh.sh_type == SHT_PROGBITS && data->d_size > 0) {
			if (sh.sh_flags & SHF_EXECINSTR) {
				if (strcmp(name, ".text") == 0)
					obj->efile.text_shndx = idx;
				err = bpf_object__add_program(obj, data->d_buf,
							      data->d_size,
							      name, idx);
				if (err) {
					char errmsg[STRERR_BUFSIZE];
					char *cp;

					cp = libbpf_strerror_r(-err, errmsg,
							       sizeof(errmsg));
					pr_warn("failed to alloc program %s (%s): %s",
						name, obj->path, cp);
					return err;
				}
			} else if (strcmp(name, DATA_SEC) == 0) {
				obj->efile.data = data;
				obj->efile.data_shndx = idx;
			} else if (strcmp(name, RODATA_SEC) == 0) {
				obj->efile.rodata = data;
				obj->efile.rodata_shndx = idx;
			} else if (strcmp(name, STRUCT_OPS_SEC) == 0) {
				obj->efile.st_ops_data = data;
				obj->efile.st_ops_shndx = idx;
			} else {
				pr_debug("skip section(%d) %s\n", idx, name);
			}
		} else if (sh.sh_type == SHT_REL) {
			int nr_sects = obj->efile.nr_reloc_sects;
			void *sects = obj->efile.reloc_sects;
			int sec = sh.sh_info; /* points to other section */

			/* Only do relo for section with exec instructions */
			if (!section_have_execinstr(obj, sec) &&
			    strcmp(name, ".rel" STRUCT_OPS_SEC)) {
				pr_debug("skip relo %s(%d) for section(%d)\n",
					 name, idx, sec);
				continue;
			}

			sects = reallocarray(sects, nr_sects + 1,
					     sizeof(*obj->efile.reloc_sects));
			if (!sects) {
				pr_warn("reloc_sects realloc failed\n");
				return -ENOMEM;
			}

			obj->efile.reloc_sects = sects;
			obj->efile.nr_reloc_sects++;

			obj->efile.reloc_sects[nr_sects].shdr = sh;
			obj->efile.reloc_sects[nr_sects].data = data;
		} else if (sh.sh_type == SHT_NOBITS &&
			   strcmp(name, BSS_SEC) == 0) {
			obj->efile.bss = data;
			obj->efile.bss_shndx = idx;
		} else {
			pr_debug("skip section(%d) %s\n", idx, name);
		}
	}

	if (!obj->efile.strtabidx || obj->efile.strtabidx > idx) {
		pr_warn("Corrupted ELF file: index of strtab invalid\n");
		return -LIBBPF_ERRNO__FORMAT;
	}
	return bpf_object__init_btf(obj, btf_data, btf_ext_data);
}

static bool sym_is_extern(const GElf_Sym *sym)
{
	int bind = GELF_ST_BIND(sym->st_info);
	/* externs are symbols w/ type=NOTYPE, bind=GLOBAL|WEAK, section=UND */
	return sym->st_shndx == SHN_UNDEF &&
	       (bind == STB_GLOBAL || bind == STB_WEAK) &&
	       GELF_ST_TYPE(sym->st_info) == STT_NOTYPE;
}

static int find_extern_btf_id(const struct btf *btf, const char *ext_name)
{
	const struct btf_type *t;
	const char *var_name;
	int i, n;

	if (!btf)
		return -ESRCH;

	n = btf__get_nr_types(btf);
	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(btf, i);

		if (!btf_is_var(t))
			continue;

		var_name = btf__name_by_offset(btf, t->name_off);
		if (strcmp(var_name, ext_name))
			continue;

		if (btf_var(t)->linkage != BTF_VAR_GLOBAL_EXTERN)
			return -EINVAL;

		return i;
	}

	return -ENOENT;
}

static enum extern_type find_extern_type(const struct btf *btf, int id,
					 bool *is_signed)
{
	const struct btf_type *t;
	const char *name;

	t = skip_mods_and_typedefs(btf, id, NULL);
	name = btf__name_by_offset(btf, t->name_off);

	if (is_signed)
		*is_signed = false;
	switch (btf_kind(t)) {
	case BTF_KIND_INT: {
		int enc = btf_int_encoding(t);

		if (enc & BTF_INT_BOOL)
			return t->size == 1 ? EXT_BOOL : EXT_UNKNOWN;
		if (is_signed)
			*is_signed = enc & BTF_INT_SIGNED;
		if (t->size == 1)
			return EXT_CHAR;
		if (t->size < 1 || t->size > 8 || (t->size & (t->size - 1)))
			return EXT_UNKNOWN;
		return EXT_INT;
	}
	case BTF_KIND_ENUM:
		if (t->size != 4)
			return EXT_UNKNOWN;
		if (strcmp(name, "libbpf_tristate"))
			return EXT_UNKNOWN;
		return EXT_TRISTATE;
	case BTF_KIND_ARRAY:
		if (btf_array(t)->nelems == 0)
			return EXT_UNKNOWN;
		if (find_extern_type(btf, btf_array(t)->type, NULL) != EXT_CHAR)
			return EXT_UNKNOWN;
		return EXT_CHAR_ARR;
	default:
		return EXT_UNKNOWN;
	}
}

static int cmp_externs(const void *_a, const void *_b)
{
	const struct extern_desc *a = _a;
	const struct extern_desc *b = _b;

	/* descending order by alignment requirements */
	if (a->align != b->align)
		return a->align > b->align ? -1 : 1;
	/* ascending order by size, within same alignment class */
	if (a->sz != b->sz)
		return a->sz < b->sz ? -1 : 1;
	/* resolve ties by name */
	return strcmp(a->name, b->name);
}

static int bpf_object__collect_externs(struct bpf_object *obj)
{
	const struct btf_type *t;
	struct extern_desc *ext;
	int i, n, off, btf_id;
	struct btf_type *sec;
	const char *ext_name;
	Elf_Scn *scn;
	GElf_Shdr sh;

	if (!obj->efile.symbols)
		return 0;

	scn = elf_getscn(obj->efile.elf, obj->efile.symbols_shndx);
	if (!scn)
		return -LIBBPF_ERRNO__FORMAT;
	if (gelf_getshdr(scn, &sh) != &sh)
		return -LIBBPF_ERRNO__FORMAT;
	n = sh.sh_size / sh.sh_entsize;

	pr_debug("looking for externs among %d symbols...\n", n);
	for (i = 0; i < n; i++) {
		GElf_Sym sym;

		if (!gelf_getsym(obj->efile.symbols, i, &sym))
			return -LIBBPF_ERRNO__FORMAT;
		if (!sym_is_extern(&sym))
			continue;
		ext_name = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				      sym.st_name);
		if (!ext_name || !ext_name[0])
			continue;

		ext = obj->externs;
		ext = reallocarray(ext, obj->nr_extern + 1, sizeof(*ext));
		if (!ext)
			return -ENOMEM;
		obj->externs = ext;
		ext = &ext[obj->nr_extern];
		memset(ext, 0, sizeof(*ext));
		obj->nr_extern++;

		ext->btf_id = find_extern_btf_id(obj->btf, ext_name);
		if (ext->btf_id <= 0) {
			pr_warn("failed to find BTF for extern '%s': %d\n",
				ext_name, ext->btf_id);
			return ext->btf_id;
		}
		t = btf__type_by_id(obj->btf, ext->btf_id);
		ext->name = btf__name_by_offset(obj->btf, t->name_off);
		ext->sym_idx = i;
		ext->is_weak = GELF_ST_BIND(sym.st_info) == STB_WEAK;
		ext->sz = btf__resolve_size(obj->btf, t->type);
		if (ext->sz <= 0) {
			pr_warn("failed to resolve size of extern '%s': %d\n",
				ext_name, ext->sz);
			return ext->sz;
		}
		ext->align = btf__align_of(obj->btf, t->type);
		if (ext->align <= 0) {
			pr_warn("failed to determine alignment of extern '%s': %d\n",
				ext_name, ext->align);
			return -EINVAL;
		}
		ext->type = find_extern_type(obj->btf, t->type,
					     &ext->is_signed);
		if (ext->type == EXT_UNKNOWN) {
			pr_warn("extern '%s' type is unsupported\n", ext_name);
			return -ENOTSUP;
		}
	}
	pr_debug("collected %d externs total\n", obj->nr_extern);

	if (!obj->nr_extern)
		return 0;

	/* sort externs by (alignment, size, name) and calculate their offsets
	 * within a map */
	qsort(obj->externs, obj->nr_extern, sizeof(*ext), cmp_externs);
	off = 0;
	for (i = 0; i < obj->nr_extern; i++) {
		ext = &obj->externs[i];
		ext->data_off = roundup(off, ext->align);
		off = ext->data_off + ext->sz;
		pr_debug("extern #%d: symbol %d, off %u, name %s\n",
			 i, ext->sym_idx, ext->data_off, ext->name);
	}

	btf_id = btf__find_by_name(obj->btf, KCONFIG_SEC);
	if (btf_id <= 0) {
		pr_warn("no BTF info found for '%s' datasec\n", KCONFIG_SEC);
		return -ESRCH;
	}

	sec = (struct btf_type *)btf__type_by_id(obj->btf, btf_id);
	sec->size = off;
	n = btf_vlen(sec);
	for (i = 0; i < n; i++) {
		struct btf_var_secinfo *vs = btf_var_secinfos(sec) + i;

		t = btf__type_by_id(obj->btf, vs->type);
		ext_name = btf__name_by_offset(obj->btf, t->name_off);
		ext = find_extern_by_name(obj, ext_name);
		if (!ext) {
			pr_warn("failed to find extern definition for BTF var '%s'\n",
				ext_name);
			return -ESRCH;
		}
		vs->offset = ext->data_off;
		btf_var(t)->linkage = BTF_VAR_GLOBAL_ALLOCATED;
	}

	return 0;
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

struct bpf_program *
bpf_object__find_program_by_name(const struct bpf_object *obj,
				 const char *name)
{
	struct bpf_program *prog;

	bpf_object__for_each_program(prog, obj) {
		if (!strcmp(prog->name, name))
			return prog;
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

static enum libbpf_map_type
bpf_object__section_to_libbpf_map_type(const struct bpf_object *obj, int shndx)
{
	if (shndx == obj->efile.data_shndx)
		return LIBBPF_MAP_DATA;
	else if (shndx == obj->efile.bss_shndx)
		return LIBBPF_MAP_BSS;
	else if (shndx == obj->efile.rodata_shndx)
		return LIBBPF_MAP_RODATA;
	else if (shndx == obj->efile.symbols_shndx)
		return LIBBPF_MAP_KCONFIG;
	else
		return LIBBPF_MAP_UNSPEC;
}

static int bpf_program__record_reloc(struct bpf_program *prog,
				     struct reloc_desc *reloc_desc,
				     __u32 insn_idx, const char *name,
				     const GElf_Sym *sym, const GElf_Rel *rel)
{
	struct bpf_insn *insn = &prog->insns[insn_idx];
	size_t map_idx, nr_maps = prog->obj->nr_maps;
	struct bpf_object *obj = prog->obj;
	__u32 shdr_idx = sym->st_shndx;
	enum libbpf_map_type type;
	struct bpf_map *map;

	/* sub-program call relocation */
	if (insn->code == (BPF_JMP | BPF_CALL)) {
		if (insn->src_reg != BPF_PSEUDO_CALL) {
			pr_warn("incorrect bpf_call opcode\n");
			return -LIBBPF_ERRNO__RELOC;
		}
		/* text_shndx can be 0, if no default "main" program exists */
		if (!shdr_idx || shdr_idx != obj->efile.text_shndx) {
			pr_warn("bad call relo against section %u\n", shdr_idx);
			return -LIBBPF_ERRNO__RELOC;
		}
		if (sym->st_value % 8) {
			pr_warn("bad call relo offset: %zu\n",
				(size_t)sym->st_value);
			return -LIBBPF_ERRNO__RELOC;
		}
		reloc_desc->type = RELO_CALL;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->sym_off = sym->st_value;
		obj->has_pseudo_calls = true;
		return 0;
	}

	if (insn->code != (BPF_LD | BPF_IMM | BPF_DW)) {
		pr_warn("invalid relo for insns[%d].code 0x%x\n",
			insn_idx, insn->code);
		return -LIBBPF_ERRNO__RELOC;
	}

	if (sym_is_extern(sym)) {
		int sym_idx = GELF_R_SYM(rel->r_info);
		int i, n = obj->nr_extern;
		struct extern_desc *ext;

		for (i = 0; i < n; i++) {
			ext = &obj->externs[i];
			if (ext->sym_idx == sym_idx)
				break;
		}
		if (i >= n) {
			pr_warn("extern relo failed to find extern for sym %d\n",
				sym_idx);
			return -LIBBPF_ERRNO__RELOC;
		}
		pr_debug("found extern #%d '%s' (sym %d, off %u) for insn %u\n",
			 i, ext->name, ext->sym_idx, ext->data_off, insn_idx);
		reloc_desc->type = RELO_EXTERN;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->sym_off = ext->data_off;
		return 0;
	}

	if (!shdr_idx || shdr_idx >= SHN_LORESERVE) {
		pr_warn("invalid relo for \'%s\' in special section 0x%x; forgot to initialize global var?..\n",
			name, shdr_idx);
		return -LIBBPF_ERRNO__RELOC;
	}

	type = bpf_object__section_to_libbpf_map_type(obj, shdr_idx);

	/* generic map reference relocation */
	if (type == LIBBPF_MAP_UNSPEC) {
		if (!bpf_object__shndx_is_maps(obj, shdr_idx)) {
			pr_warn("bad map relo against section %u\n",
				shdr_idx);
			return -LIBBPF_ERRNO__RELOC;
		}
		for (map_idx = 0; map_idx < nr_maps; map_idx++) {
			map = &obj->maps[map_idx];
			if (map->libbpf_type != type ||
			    map->sec_idx != sym->st_shndx ||
			    map->sec_offset != sym->st_value)
				continue;
			pr_debug("found map %zd (%s, sec %d, off %zu) for insn %u\n",
				 map_idx, map->name, map->sec_idx,
				 map->sec_offset, insn_idx);
			break;
		}
		if (map_idx >= nr_maps) {
			pr_warn("map relo failed to find map for sec %u, off %zu\n",
				shdr_idx, (size_t)sym->st_value);
			return -LIBBPF_ERRNO__RELOC;
		}
		reloc_desc->type = RELO_LD64;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->map_idx = map_idx;
		reloc_desc->sym_off = 0; /* sym->st_value determines map_idx */
		return 0;
	}

	/* global data map relocation */
	if (!bpf_object__shndx_is_data(obj, shdr_idx)) {
		pr_warn("bad data relo against section %u\n", shdr_idx);
		return -LIBBPF_ERRNO__RELOC;
	}
	for (map_idx = 0; map_idx < nr_maps; map_idx++) {
		map = &obj->maps[map_idx];
		if (map->libbpf_type != type)
			continue;
		pr_debug("found data map %zd (%s, sec %d, off %zu) for insn %u\n",
			 map_idx, map->name, map->sec_idx, map->sec_offset,
			 insn_idx);
		break;
	}
	if (map_idx >= nr_maps) {
		pr_warn("data relo failed to find map for sec %u\n",
			shdr_idx);
		return -LIBBPF_ERRNO__RELOC;
	}

	reloc_desc->type = RELO_DATA;
	reloc_desc->insn_idx = insn_idx;
	reloc_desc->map_idx = map_idx;
	reloc_desc->sym_off = sym->st_value;
	return 0;
}

static int
bpf_program__collect_reloc(struct bpf_program *prog, GElf_Shdr *shdr,
			   Elf_Data *data, struct bpf_object *obj)
{
	Elf_Data *symbols = obj->efile.symbols;
	int err, i, nrels;

	pr_debug("collecting relocating info for: '%s'\n", prog->section_name);
	nrels = shdr->sh_size / shdr->sh_entsize;

	prog->reloc_desc = malloc(sizeof(*prog->reloc_desc) * nrels);
	if (!prog->reloc_desc) {
		pr_warn("failed to alloc memory in relocation\n");
		return -ENOMEM;
	}
	prog->nr_reloc = nrels;

	for (i = 0; i < nrels; i++) {
		const char *name;
		__u32 insn_idx;
		GElf_Sym sym;
		GElf_Rel rel;

		if (!gelf_getrel(data, i, &rel)) {
			pr_warn("relocation: failed to get %d reloc\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}
		if (!gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym)) {
			pr_warn("relocation: symbol %"PRIx64" not found\n",
				GELF_R_SYM(rel.r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}
		if (rel.r_offset % sizeof(struct bpf_insn))
			return -LIBBPF_ERRNO__FORMAT;

		insn_idx = rel.r_offset / sizeof(struct bpf_insn);
		name = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				  sym.st_name) ? : "<?>";

		pr_debug("relo for shdr %u, symb %zu, value %zu, type %d, bind %d, name %d (\'%s\'), insn %u\n",
			 (__u32)sym.st_shndx, (size_t)GELF_R_SYM(rel.r_info),
			 (size_t)sym.st_value, GELF_ST_TYPE(sym.st_info),
			 GELF_ST_BIND(sym.st_info), sym.st_name, name,
			 insn_idx);

		err = bpf_program__record_reloc(prog, &prog->reloc_desc[i],
						insn_idx, name, &sym, &rel);
		if (err)
			return err;
	}
	return 0;
}

static int bpf_map_find_btf_info(struct bpf_object *obj, struct bpf_map *map)
{
	struct bpf_map_def *def = &map->def;
	__u32 key_type_id = 0, value_type_id = 0;
	int ret;

	/* if it's BTF-defined map, we don't need to search for type IDs.
	 * For struct_ops map, it does not need btf_key_type_id and
	 * btf_value_type_id.
	 */
	if (map->sec_idx == obj->efile.btf_maps_shndx ||
	    bpf_map__is_struct_ops(map))
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
	if (new_fd < 0) {
		err = -errno;
		goto err_free_new_name;
	}

	new_fd = dup3(fd, new_fd, O_CLOEXEC);
	if (new_fd < 0) {
		err = -errno;
		goto err_close_new_fd;
	}

	err = zclose(map->fd);
	if (err) {
		err = -errno;
		goto err_close_new_fd;
	}
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
	map->reused = true;

	return 0;

err_close_new_fd:
	close(new_fd);
err_free_new_name:
	free(new_name);
	return err;
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
		pr_warn("Error in %s():%s(%d). Couldn't load basic 'r0 = 0' BPF program.\n",
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
		pr_warn("Error in %s():%s(%d). Couldn't create simple array map.\n",
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
	static const char strs[] = "\0int\0x\0a";
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

static int bpf_object__probe_btf_func_global(struct bpf_object *obj)
{
	static const char strs[] = "\0int\0x\0a";
	/* static void x(int a) {} */
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),  /* [1] */
		/* FUNC_PROTO */                                /* [2] */
		BTF_TYPE_ENC(0, BTF_INFO_ENC(BTF_KIND_FUNC_PROTO, 0, 1), 0),
		BTF_PARAM_ENC(7, 1),
		/* FUNC x BTF_FUNC_GLOBAL */                    /* [3] */
		BTF_TYPE_ENC(5, BTF_INFO_ENC(BTF_KIND_FUNC, 0, BTF_FUNC_GLOBAL), 2),
	};
	int btf_fd;

	btf_fd = libbpf__load_raw_btf((char *)types, sizeof(types),
				      strs, sizeof(strs));
	if (btf_fd >= 0) {
		obj->caps.btf_func_global = 1;
		close(btf_fd);
		return 1;
	}

	return 0;
}

static int bpf_object__probe_btf_datasec(struct bpf_object *obj)
{
	static const char strs[] = "\0x\0.data";
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

static int bpf_object__probe_array_mmap(struct bpf_object *obj)
{
	struct bpf_create_map_attr attr = {
		.map_type = BPF_MAP_TYPE_ARRAY,
		.map_flags = BPF_F_MMAPABLE,
		.key_size = sizeof(int),
		.value_size = sizeof(int),
		.max_entries = 1,
	};
	int fd;

	fd = bpf_create_map_xattr(&attr);
	if (fd >= 0) {
		obj->caps.array_mmap = 1;
		close(fd);
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
		bpf_object__probe_btf_func_global,
		bpf_object__probe_btf_datasec,
		bpf_object__probe_array_mmap,
	};
	int i, ret;

	for (i = 0; i < ARRAY_SIZE(probe_fn); i++) {
		ret = probe_fn[i](obj);
		if (ret < 0)
			pr_debug("Probe #%d failed with %d.\n", i, ret);
	}

	return 0;
}

static bool map_is_reuse_compat(const struct bpf_map *map, int map_fd)
{
	struct bpf_map_info map_info = {};
	char msg[STRERR_BUFSIZE];
	__u32 map_info_len;

	map_info_len = sizeof(map_info);

	if (bpf_obj_get_info_by_fd(map_fd, &map_info, &map_info_len)) {
		pr_warn("failed to get map info for map FD %d: %s\n",
			map_fd, libbpf_strerror_r(errno, msg, sizeof(msg)));
		return false;
	}

	return (map_info.type == map->def.type &&
		map_info.key_size == map->def.key_size &&
		map_info.value_size == map->def.value_size &&
		map_info.max_entries == map->def.max_entries &&
		map_info.map_flags == map->def.map_flags);
}

static int
bpf_object__reuse_map(struct bpf_map *map)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err, pin_fd;

	pin_fd = bpf_obj_get(map->pin_path);
	if (pin_fd < 0) {
		err = -errno;
		if (err == -ENOENT) {
			pr_debug("found no pinned map to reuse at '%s'\n",
				 map->pin_path);
			return 0;
		}

		cp = libbpf_strerror_r(-err, errmsg, sizeof(errmsg));
		pr_warn("couldn't retrieve pinned map '%s': %s\n",
			map->pin_path, cp);
		return err;
	}

	if (!map_is_reuse_compat(map, pin_fd)) {
		pr_warn("couldn't reuse pinned map at '%s': parameter mismatch\n",
			map->pin_path);
		close(pin_fd);
		return -EINVAL;
	}

	err = bpf_map__reuse_fd(map, pin_fd);
	if (err) {
		close(pin_fd);
		return err;
	}
	map->pinned = true;
	pr_debug("reused pinned map at '%s'\n", map->pin_path);

	return 0;
}

static int
bpf_object__populate_internal_map(struct bpf_object *obj, struct bpf_map *map)
{
	enum libbpf_map_type map_type = map->libbpf_type;
	char *cp, errmsg[STRERR_BUFSIZE];
	int err, zero = 0;

	/* kernel already zero-initializes .bss map. */
	if (map_type == LIBBPF_MAP_BSS)
		return 0;

	err = bpf_map_update_elem(map->fd, &zero, map->mmaped, 0);
	if (err) {
		err = -errno;
		cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
		pr_warn("Error setting initial map(%s) contents: %s\n",
			map->name, cp);
		return err;
	}

	/* Freeze .rodata and .kconfig map as read-only from syscall side. */
	if (map_type == LIBBPF_MAP_RODATA || map_type == LIBBPF_MAP_KCONFIG) {
		err = bpf_map_freeze(map->fd);
		if (err) {
			err = -errno;
			cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
			pr_warn("Error freezing map(%s) as read-only: %s\n",
				map->name, cp);
			return err;
		}
	}
	return 0;
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

		if (map->pin_path) {
			err = bpf_object__reuse_map(map);
			if (err) {
				pr_warn("error reusing pinned map %s\n",
					map->name);
				return err;
			}
		}

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
				pr_warn("failed to determine number of system CPUs: %d\n",
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
		if (bpf_map__is_struct_ops(map))
			create_attr.btf_vmlinux_value_type_id =
				map->btf_vmlinux_value_type_id;

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
			pr_warn("Error in bpf_create_map_xattr(%s):%s(%d). Retrying without BTF.\n",
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
			pr_warn("failed to create map (name: '%s'): %s(%d)\n",
				map->name, cp, err);
			pr_perm_msg(err);
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

		if (map->pin_path && !map->pinned) {
			err = bpf_map__pin(map, NULL);
			if (err) {
				pr_warn("failed to auto-pin map name '%s' at '%s'\n",
					map->name, map->pin_path);
				return err;
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
		pr_warn("Error in loading %s for sec %s.\n",
			info_name, prog->section_name);
		return err;
	}

	/* err == -ENOENT (i.e. prog->section_name not found in btf_ext) */

	if (btf_prog_info) {
		/*
		 * Some info has already been found but has problem
		 * in the last btf_ext reloc. Must have to error out.
		 */
		pr_warn("Error in relocating %s for sec %s.\n",
			info_name, prog->section_name);
		return err;
	}

	/* Have problem loading the very first info. Ignore the rest. */
	pr_warn("Cannot find %s for main program sec %s. Ignore all %s.\n",
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

#define BPF_CORE_SPEC_MAX_LEN 64

/* represents BPF CO-RE field or array element accessor */
struct bpf_core_accessor {
	__u32 type_id;		/* struct/union type or array element type */
	__u32 idx;		/* field index or array index */
	const char *name;	/* field name or NULL for array accessor */
};

struct bpf_core_spec {
	const struct btf *btf;
	/* high-level spec: named fields and array indices only */
	struct bpf_core_accessor spec[BPF_CORE_SPEC_MAX_LEN];
	/* high-level spec length */
	int len;
	/* raw, low-level spec: 1-to-1 with accessor spec string */
	int raw_spec[BPF_CORE_SPEC_MAX_LEN];
	/* raw spec length */
	int raw_len;
	/* field bit offset represented by spec */
	__u32 bit_offset;
};

static bool str_is_empty(const char *s)
{
	return !s || !s[0];
}

static bool is_flex_arr(const struct btf *btf,
			const struct bpf_core_accessor *acc,
			const struct btf_array *arr)
{
	const struct btf_type *t;

	/* not a flexible array, if not inside a struct or has non-zero size */
	if (!acc->name || arr->nelems > 0)
		return false;

	/* has to be the last member of enclosing struct */
	t = btf__type_by_id(btf, acc->type_id);
	return acc->idx == btf_vlen(t) - 1;
}

/*
 * Turn bpf_field_reloc into a low- and high-level spec representation,
 * validating correctness along the way, as well as calculating resulting
 * field bit offset, specified by accessor string. Low-level spec captures
 * every single level of nestedness, including traversing anonymous
 * struct/union members. High-level one only captures semantically meaningful
 * "turning points": named fields and array indicies.
 * E.g., for this case:
 *
 *   struct sample {
 *       int __unimportant;
 *       struct {
 *           int __1;
 *           int __2;
 *           int a[7];
 *       };
 *   };
 *
 *   struct sample *s = ...;
 *
 *   int x = &s->a[3]; // access string = '0:1:2:3'
 *
 * Low-level spec has 1:1 mapping with each element of access string (it's
 * just a parsed access string representation): [0, 1, 2, 3].
 *
 * High-level spec will capture only 3 points:
 *   - intial zero-index access by pointer (&s->... is the same as &s[0]...);
 *   - field 'a' access (corresponds to '2' in low-level spec);
 *   - array element #3 access (corresponds to '3' in low-level spec).
 *
 */
static int bpf_core_spec_parse(const struct btf *btf,
			       __u32 type_id,
			       const char *spec_str,
			       struct bpf_core_spec *spec)
{
	int access_idx, parsed_len, i;
	struct bpf_core_accessor *acc;
	const struct btf_type *t;
	const char *name;
	__u32 id;
	__s64 sz;

	if (str_is_empty(spec_str) || *spec_str == ':')
		return -EINVAL;

	memset(spec, 0, sizeof(*spec));
	spec->btf = btf;

	/* parse spec_str="0:1:2:3:4" into array raw_spec=[0, 1, 2, 3, 4] */
	while (*spec_str) {
		if (*spec_str == ':')
			++spec_str;
		if (sscanf(spec_str, "%d%n", &access_idx, &parsed_len) != 1)
			return -EINVAL;
		if (spec->raw_len == BPF_CORE_SPEC_MAX_LEN)
			return -E2BIG;
		spec_str += parsed_len;
		spec->raw_spec[spec->raw_len++] = access_idx;
	}

	if (spec->raw_len == 0)
		return -EINVAL;

	/* first spec value is always reloc type array index */
	t = skip_mods_and_typedefs(btf, type_id, &id);
	if (!t)
		return -EINVAL;

	access_idx = spec->raw_spec[0];
	spec->spec[0].type_id = id;
	spec->spec[0].idx = access_idx;
	spec->len++;

	sz = btf__resolve_size(btf, id);
	if (sz < 0)
		return sz;
	spec->bit_offset = access_idx * sz * 8;

	for (i = 1; i < spec->raw_len; i++) {
		t = skip_mods_and_typedefs(btf, id, &id);
		if (!t)
			return -EINVAL;

		access_idx = spec->raw_spec[i];
		acc = &spec->spec[spec->len];

		if (btf_is_composite(t)) {
			const struct btf_member *m;
			__u32 bit_offset;

			if (access_idx >= btf_vlen(t))
				return -EINVAL;

			bit_offset = btf_member_bit_offset(t, access_idx);
			spec->bit_offset += bit_offset;

			m = btf_members(t) + access_idx;
			if (m->name_off) {
				name = btf__name_by_offset(btf, m->name_off);
				if (str_is_empty(name))
					return -EINVAL;

				acc->type_id = id;
				acc->idx = access_idx;
				acc->name = name;
				spec->len++;
			}

			id = m->type;
		} else if (btf_is_array(t)) {
			const struct btf_array *a = btf_array(t);
			bool flex;

			t = skip_mods_and_typedefs(btf, a->type, &id);
			if (!t)
				return -EINVAL;

			flex = is_flex_arr(btf, acc - 1, a);
			if (!flex && access_idx >= a->nelems)
				return -EINVAL;

			spec->spec[spec->len].type_id = id;
			spec->spec[spec->len].idx = access_idx;
			spec->len++;

			sz = btf__resolve_size(btf, id);
			if (sz < 0)
				return sz;
			spec->bit_offset += access_idx * sz * 8;
		} else {
			pr_warn("relo for [%u] %s (at idx %d) captures type [%d] of unexpected kind %d\n",
				type_id, spec_str, i, id, btf_kind(t));
			return -EINVAL;
		}
	}

	return 0;
}

static bool bpf_core_is_flavor_sep(const char *s)
{
	/* check X___Y name pattern, where X and Y are not underscores */
	return s[0] != '_' &&				      /* X */
	       s[1] == '_' && s[2] == '_' && s[3] == '_' &&   /* ___ */
	       s[4] != '_';				      /* Y */
}

/* Given 'some_struct_name___with_flavor' return the length of a name prefix
 * before last triple underscore. Struct name part after last triple
 * underscore is ignored by BPF CO-RE relocation during relocation matching.
 */
static size_t bpf_core_essential_name_len(const char *name)
{
	size_t n = strlen(name);
	int i;

	for (i = n - 5; i >= 0; i--) {
		if (bpf_core_is_flavor_sep(name + i))
			return i + 1;
	}
	return n;
}

/* dynamically sized list of type IDs */
struct ids_vec {
	__u32 *data;
	int len;
};

static void bpf_core_free_cands(struct ids_vec *cand_ids)
{
	free(cand_ids->data);
	free(cand_ids);
}

static struct ids_vec *bpf_core_find_cands(const struct btf *local_btf,
					   __u32 local_type_id,
					   const struct btf *targ_btf)
{
	size_t local_essent_len, targ_essent_len;
	const char *local_name, *targ_name;
	const struct btf_type *t;
	struct ids_vec *cand_ids;
	__u32 *new_ids;
	int i, err, n;

	t = btf__type_by_id(local_btf, local_type_id);
	if (!t)
		return ERR_PTR(-EINVAL);

	local_name = btf__name_by_offset(local_btf, t->name_off);
	if (str_is_empty(local_name))
		return ERR_PTR(-EINVAL);
	local_essent_len = bpf_core_essential_name_len(local_name);

	cand_ids = calloc(1, sizeof(*cand_ids));
	if (!cand_ids)
		return ERR_PTR(-ENOMEM);

	n = btf__get_nr_types(targ_btf);
	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(targ_btf, i);
		targ_name = btf__name_by_offset(targ_btf, t->name_off);
		if (str_is_empty(targ_name))
			continue;

		targ_essent_len = bpf_core_essential_name_len(targ_name);
		if (targ_essent_len != local_essent_len)
			continue;

		if (strncmp(local_name, targ_name, local_essent_len) == 0) {
			pr_debug("[%d] %s: found candidate [%d] %s\n",
				 local_type_id, local_name, i, targ_name);
			new_ids = reallocarray(cand_ids->data,
					       cand_ids->len + 1,
					       sizeof(*cand_ids->data));
			if (!new_ids) {
				err = -ENOMEM;
				goto err_out;
			}
			cand_ids->data = new_ids;
			cand_ids->data[cand_ids->len++] = i;
		}
	}
	return cand_ids;
err_out:
	bpf_core_free_cands(cand_ids);
	return ERR_PTR(err);
}

/* Check two types for compatibility, skipping const/volatile/restrict and
 * typedefs, to ensure we are relocating compatible entities:
 *   - any two STRUCTs/UNIONs are compatible and can be mixed;
 *   - any two FWDs are compatible, if their names match (modulo flavor suffix);
 *   - any two PTRs are always compatible;
 *   - for ENUMs, names should be the same (ignoring flavor suffix) or at
 *     least one of enums should be anonymous;
 *   - for ENUMs, check sizes, names are ignored;
 *   - for INT, size and signedness are ignored;
 *   - for ARRAY, dimensionality is ignored, element types are checked for
 *     compatibility recursively;
 *   - everything else shouldn't be ever a target of relocation.
 * These rules are not set in stone and probably will be adjusted as we get
 * more experience with using BPF CO-RE relocations.
 */
static int bpf_core_fields_are_compat(const struct btf *local_btf,
				      __u32 local_id,
				      const struct btf *targ_btf,
				      __u32 targ_id)
{
	const struct btf_type *local_type, *targ_type;

recur:
	local_type = skip_mods_and_typedefs(local_btf, local_id, &local_id);
	targ_type = skip_mods_and_typedefs(targ_btf, targ_id, &targ_id);
	if (!local_type || !targ_type)
		return -EINVAL;

	if (btf_is_composite(local_type) && btf_is_composite(targ_type))
		return 1;
	if (btf_kind(local_type) != btf_kind(targ_type))
		return 0;

	switch (btf_kind(local_type)) {
	case BTF_KIND_PTR:
		return 1;
	case BTF_KIND_FWD:
	case BTF_KIND_ENUM: {
		const char *local_name, *targ_name;
		size_t local_len, targ_len;

		local_name = btf__name_by_offset(local_btf,
						 local_type->name_off);
		targ_name = btf__name_by_offset(targ_btf, targ_type->name_off);
		local_len = bpf_core_essential_name_len(local_name);
		targ_len = bpf_core_essential_name_len(targ_name);
		/* one of them is anonymous or both w/ same flavor-less names */
		return local_len == 0 || targ_len == 0 ||
		       (local_len == targ_len &&
			strncmp(local_name, targ_name, local_len) == 0);
	}
	case BTF_KIND_INT:
		/* just reject deprecated bitfield-like integers; all other
		 * integers are by default compatible between each other
		 */
		return btf_int_offset(local_type) == 0 &&
		       btf_int_offset(targ_type) == 0;
	case BTF_KIND_ARRAY:
		local_id = btf_array(local_type)->type;
		targ_id = btf_array(targ_type)->type;
		goto recur;
	default:
		pr_warn("unexpected kind %d relocated, local [%d], target [%d]\n",
			btf_kind(local_type), local_id, targ_id);
		return 0;
	}
}

/*
 * Given single high-level named field accessor in local type, find
 * corresponding high-level accessor for a target type. Along the way,
 * maintain low-level spec for target as well. Also keep updating target
 * bit offset.
 *
 * Searching is performed through recursive exhaustive enumeration of all
 * fields of a struct/union. If there are any anonymous (embedded)
 * structs/unions, they are recursively searched as well. If field with
 * desired name is found, check compatibility between local and target types,
 * before returning result.
 *
 * 1 is returned, if field is found.
 * 0 is returned if no compatible field is found.
 * <0 is returned on error.
 */
static int bpf_core_match_member(const struct btf *local_btf,
				 const struct bpf_core_accessor *local_acc,
				 const struct btf *targ_btf,
				 __u32 targ_id,
				 struct bpf_core_spec *spec,
				 __u32 *next_targ_id)
{
	const struct btf_type *local_type, *targ_type;
	const struct btf_member *local_member, *m;
	const char *local_name, *targ_name;
	__u32 local_id;
	int i, n, found;

	targ_type = skip_mods_and_typedefs(targ_btf, targ_id, &targ_id);
	if (!targ_type)
		return -EINVAL;
	if (!btf_is_composite(targ_type))
		return 0;

	local_id = local_acc->type_id;
	local_type = btf__type_by_id(local_btf, local_id);
	local_member = btf_members(local_type) + local_acc->idx;
	local_name = btf__name_by_offset(local_btf, local_member->name_off);

	n = btf_vlen(targ_type);
	m = btf_members(targ_type);
	for (i = 0; i < n; i++, m++) {
		__u32 bit_offset;

		bit_offset = btf_member_bit_offset(targ_type, i);

		/* too deep struct/union/array nesting */
		if (spec->raw_len == BPF_CORE_SPEC_MAX_LEN)
			return -E2BIG;

		/* speculate this member will be the good one */
		spec->bit_offset += bit_offset;
		spec->raw_spec[spec->raw_len++] = i;

		targ_name = btf__name_by_offset(targ_btf, m->name_off);
		if (str_is_empty(targ_name)) {
			/* embedded struct/union, we need to go deeper */
			found = bpf_core_match_member(local_btf, local_acc,
						      targ_btf, m->type,
						      spec, next_targ_id);
			if (found) /* either found or error */
				return found;
		} else if (strcmp(local_name, targ_name) == 0) {
			/* matching named field */
			struct bpf_core_accessor *targ_acc;

			targ_acc = &spec->spec[spec->len++];
			targ_acc->type_id = targ_id;
			targ_acc->idx = i;
			targ_acc->name = targ_name;

			*next_targ_id = m->type;
			found = bpf_core_fields_are_compat(local_btf,
							   local_member->type,
							   targ_btf, m->type);
			if (!found)
				spec->len--; /* pop accessor */
			return found;
		}
		/* member turned out not to be what we looked for */
		spec->bit_offset -= bit_offset;
		spec->raw_len--;
	}

	return 0;
}

/*
 * Try to match local spec to a target type and, if successful, produce full
 * target spec (high-level, low-level + bit offset).
 */
static int bpf_core_spec_match(struct bpf_core_spec *local_spec,
			       const struct btf *targ_btf, __u32 targ_id,
			       struct bpf_core_spec *targ_spec)
{
	const struct btf_type *targ_type;
	const struct bpf_core_accessor *local_acc;
	struct bpf_core_accessor *targ_acc;
	int i, sz, matched;

	memset(targ_spec, 0, sizeof(*targ_spec));
	targ_spec->btf = targ_btf;

	local_acc = &local_spec->spec[0];
	targ_acc = &targ_spec->spec[0];

	for (i = 0; i < local_spec->len; i++, local_acc++, targ_acc++) {
		targ_type = skip_mods_and_typedefs(targ_spec->btf, targ_id,
						   &targ_id);
		if (!targ_type)
			return -EINVAL;

		if (local_acc->name) {
			matched = bpf_core_match_member(local_spec->btf,
							local_acc,
							targ_btf, targ_id,
							targ_spec, &targ_id);
			if (matched <= 0)
				return matched;
		} else {
			/* for i=0, targ_id is already treated as array element
			 * type (because it's the original struct), for others
			 * we should find array element type first
			 */
			if (i > 0) {
				const struct btf_array *a;
				bool flex;

				if (!btf_is_array(targ_type))
					return 0;

				a = btf_array(targ_type);
				flex = is_flex_arr(targ_btf, targ_acc - 1, a);
				if (!flex && local_acc->idx >= a->nelems)
					return 0;
				if (!skip_mods_and_typedefs(targ_btf, a->type,
							    &targ_id))
					return -EINVAL;
			}

			/* too deep struct/union/array nesting */
			if (targ_spec->raw_len == BPF_CORE_SPEC_MAX_LEN)
				return -E2BIG;

			targ_acc->type_id = targ_id;
			targ_acc->idx = local_acc->idx;
			targ_acc->name = NULL;
			targ_spec->len++;
			targ_spec->raw_spec[targ_spec->raw_len] = targ_acc->idx;
			targ_spec->raw_len++;

			sz = btf__resolve_size(targ_btf, targ_id);
			if (sz < 0)
				return sz;
			targ_spec->bit_offset += local_acc->idx * sz * 8;
		}
	}

	return 1;
}

static int bpf_core_calc_field_relo(const struct bpf_program *prog,
				    const struct bpf_field_reloc *relo,
				    const struct bpf_core_spec *spec,
				    __u32 *val, bool *validate)
{
	const struct bpf_core_accessor *acc = &spec->spec[spec->len - 1];
	const struct btf_type *t = btf__type_by_id(spec->btf, acc->type_id);
	__u32 byte_off, byte_sz, bit_off, bit_sz;
	const struct btf_member *m;
	const struct btf_type *mt;
	bool bitfield;
	__s64 sz;

	/* a[n] accessor needs special handling */
	if (!acc->name) {
		if (relo->kind == BPF_FIELD_BYTE_OFFSET) {
			*val = spec->bit_offset / 8;
		} else if (relo->kind == BPF_FIELD_BYTE_SIZE) {
			sz = btf__resolve_size(spec->btf, acc->type_id);
			if (sz < 0)
				return -EINVAL;
			*val = sz;
		} else {
			pr_warn("prog '%s': relo %d at insn #%d can't be applied to array access\n",
				bpf_program__title(prog, false),
				relo->kind, relo->insn_off / 8);
			return -EINVAL;
		}
		if (validate)
			*validate = true;
		return 0;
	}

	m = btf_members(t) + acc->idx;
	mt = skip_mods_and_typedefs(spec->btf, m->type, NULL);
	bit_off = spec->bit_offset;
	bit_sz = btf_member_bitfield_size(t, acc->idx);

	bitfield = bit_sz > 0;
	if (bitfield) {
		byte_sz = mt->size;
		byte_off = bit_off / 8 / byte_sz * byte_sz;
		/* figure out smallest int size necessary for bitfield load */
		while (bit_off + bit_sz - byte_off * 8 > byte_sz * 8) {
			if (byte_sz >= 8) {
				/* bitfield can't be read with 64-bit read */
				pr_warn("prog '%s': relo %d at insn #%d can't be satisfied for bitfield\n",
					bpf_program__title(prog, false),
					relo->kind, relo->insn_off / 8);
				return -E2BIG;
			}
			byte_sz *= 2;
			byte_off = bit_off / 8 / byte_sz * byte_sz;
		}
	} else {
		sz = btf__resolve_size(spec->btf, m->type);
		if (sz < 0)
			return -EINVAL;
		byte_sz = sz;
		byte_off = spec->bit_offset / 8;
		bit_sz = byte_sz * 8;
	}

	/* for bitfields, all the relocatable aspects are ambiguous and we
	 * might disagree with compiler, so turn off validation of expected
	 * value, except for signedness
	 */
	if (validate)
		*validate = !bitfield;

	switch (relo->kind) {
	case BPF_FIELD_BYTE_OFFSET:
		*val = byte_off;
		break;
	case BPF_FIELD_BYTE_SIZE:
		*val = byte_sz;
		break;
	case BPF_FIELD_SIGNED:
		/* enums will be assumed unsigned */
		*val = btf_is_enum(mt) ||
		       (btf_int_encoding(mt) & BTF_INT_SIGNED);
		if (validate)
			*validate = true; /* signedness is never ambiguous */
		break;
	case BPF_FIELD_LSHIFT_U64:
#if __BYTE_ORDER == __LITTLE_ENDIAN
		*val = 64 - (bit_off + bit_sz - byte_off  * 8);
#else
		*val = (8 - byte_sz) * 8 + (bit_off - byte_off * 8);
#endif
		break;
	case BPF_FIELD_RSHIFT_U64:
		*val = 64 - bit_sz;
		if (validate)
			*validate = true; /* right shift is never ambiguous */
		break;
	case BPF_FIELD_EXISTS:
	default:
		pr_warn("prog '%s': unknown relo %d at insn #%d\n",
			bpf_program__title(prog, false),
			relo->kind, relo->insn_off / 8);
		return -EINVAL;
	}

	return 0;
}

/*
 * Patch relocatable BPF instruction.
 *
 * Patched value is determined by relocation kind and target specification.
 * For field existence relocation target spec will be NULL if field is not
 * found.
 * Expected insn->imm value is determined using relocation kind and local
 * spec, and is checked before patching instruction. If actual insn->imm value
 * is wrong, bail out with error.
 *
 * Currently three kinds of BPF instructions are supported:
 * 1. rX = <imm> (assignment with immediate operand);
 * 2. rX += <imm> (arithmetic operations with immediate operand);
 */
static int bpf_core_reloc_insn(struct bpf_program *prog,
			       const struct bpf_field_reloc *relo,
			       int relo_idx,
			       const struct bpf_core_spec *local_spec,
			       const struct bpf_core_spec *targ_spec)
{
	__u32 orig_val, new_val;
	struct bpf_insn *insn;
	bool validate = true;
	int insn_idx, err;
	__u8 class;

	if (relo->insn_off % sizeof(struct bpf_insn))
		return -EINVAL;
	insn_idx = relo->insn_off / sizeof(struct bpf_insn);
	insn = &prog->insns[insn_idx];
	class = BPF_CLASS(insn->code);

	if (relo->kind == BPF_FIELD_EXISTS) {
		orig_val = 1; /* can't generate EXISTS relo w/o local field */
		new_val = targ_spec ? 1 : 0;
	} else if (!targ_spec) {
		pr_debug("prog '%s': relo #%d: substituting insn #%d w/ invalid insn\n",
			 bpf_program__title(prog, false), relo_idx, insn_idx);
		insn->code = BPF_JMP | BPF_CALL;
		insn->dst_reg = 0;
		insn->src_reg = 0;
		insn->off = 0;
		/* if this instruction is reachable (not a dead code),
		 * verifier will complain with the following message:
		 * invalid func unknown#195896080
		 */
		insn->imm = 195896080; /* => 0xbad2310 => "bad relo" */
		return 0;
	} else {
		err = bpf_core_calc_field_relo(prog, relo, local_spec,
					       &orig_val, &validate);
		if (err)
			return err;
		err = bpf_core_calc_field_relo(prog, relo, targ_spec,
					       &new_val, NULL);
		if (err)
			return err;
	}

	switch (class) {
	case BPF_ALU:
	case BPF_ALU64:
		if (BPF_SRC(insn->code) != BPF_K)
			return -EINVAL;
		if (validate && insn->imm != orig_val) {
			pr_warn("prog '%s': relo #%d: unexpected insn #%d (ALU/ALU64) value: got %u, exp %u -> %u\n",
				bpf_program__title(prog, false), relo_idx,
				insn_idx, insn->imm, orig_val, new_val);
			return -EINVAL;
		}
		orig_val = insn->imm;
		insn->imm = new_val;
		pr_debug("prog '%s': relo #%d: patched insn #%d (ALU/ALU64) imm %u -> %u\n",
			 bpf_program__title(prog, false), relo_idx, insn_idx,
			 orig_val, new_val);
		break;
	case BPF_LDX:
	case BPF_ST:
	case BPF_STX:
		if (validate && insn->off != orig_val) {
			pr_warn("prog '%s': relo #%d: unexpected insn #%d (LD/LDX/ST/STX) value: got %u, exp %u -> %u\n",
				bpf_program__title(prog, false), relo_idx,
				insn_idx, insn->off, orig_val, new_val);
			return -EINVAL;
		}
		if (new_val > SHRT_MAX) {
			pr_warn("prog '%s': relo #%d: insn #%d (LDX/ST/STX) value too big: %u\n",
				bpf_program__title(prog, false), relo_idx,
				insn_idx, new_val);
			return -ERANGE;
		}
		orig_val = insn->off;
		insn->off = new_val;
		pr_debug("prog '%s': relo #%d: patched insn #%d (LDX/ST/STX) off %u -> %u\n",
			 bpf_program__title(prog, false), relo_idx, insn_idx,
			 orig_val, new_val);
		break;
	default:
		pr_warn("prog '%s': relo #%d: trying to relocate unrecognized insn #%d, code:%x, src:%x, dst:%x, off:%x, imm:%x\n",
			bpf_program__title(prog, false), relo_idx,
			insn_idx, insn->code, insn->src_reg, insn->dst_reg,
			insn->off, insn->imm);
		return -EINVAL;
	}

	return 0;
}

/* Output spec definition in the format:
 * [<type-id>] (<type-name>) + <raw-spec> => <offset>@<spec>,
 * where <spec> is a C-syntax view of recorded field access, e.g.: x.a[3].b
 */
static void bpf_core_dump_spec(int level, const struct bpf_core_spec *spec)
{
	const struct btf_type *t;
	const char *s;
	__u32 type_id;
	int i;

	type_id = spec->spec[0].type_id;
	t = btf__type_by_id(spec->btf, type_id);
	s = btf__name_by_offset(spec->btf, t->name_off);
	libbpf_print(level, "[%u] %s + ", type_id, s);

	for (i = 0; i < spec->raw_len; i++)
		libbpf_print(level, "%d%s", spec->raw_spec[i],
			     i == spec->raw_len - 1 ? " => " : ":");

	libbpf_print(level, "%u.%u @ &x",
		     spec->bit_offset / 8, spec->bit_offset % 8);

	for (i = 0; i < spec->len; i++) {
		if (spec->spec[i].name)
			libbpf_print(level, ".%s", spec->spec[i].name);
		else
			libbpf_print(level, "[%u]", spec->spec[i].idx);
	}

}

static size_t bpf_core_hash_fn(const void *key, void *ctx)
{
	return (size_t)key;
}

static bool bpf_core_equal_fn(const void *k1, const void *k2, void *ctx)
{
	return k1 == k2;
}

static void *u32_as_hash_key(__u32 x)
{
	return (void *)(uintptr_t)x;
}

/*
 * CO-RE relocate single instruction.
 *
 * The outline and important points of the algorithm:
 * 1. For given local type, find corresponding candidate target types.
 *    Candidate type is a type with the same "essential" name, ignoring
 *    everything after last triple underscore (___). E.g., `sample`,
 *    `sample___flavor_one`, `sample___flavor_another_one`, are all candidates
 *    for each other. Names with triple underscore are referred to as
 *    "flavors" and are useful, among other things, to allow to
 *    specify/support incompatible variations of the same kernel struct, which
 *    might differ between different kernel versions and/or build
 *    configurations.
 *
 *    N.B. Struct "flavors" could be generated by bpftool's BTF-to-C
 *    converter, when deduplicated BTF of a kernel still contains more than
 *    one different types with the same name. In that case, ___2, ___3, etc
 *    are appended starting from second name conflict. But start flavors are
 *    also useful to be defined "locally", in BPF program, to extract same
 *    data from incompatible changes between different kernel
 *    versions/configurations. For instance, to handle field renames between
 *    kernel versions, one can use two flavors of the struct name with the
 *    same common name and use conditional relocations to extract that field,
 *    depending on target kernel version.
 * 2. For each candidate type, try to match local specification to this
 *    candidate target type. Matching involves finding corresponding
 *    high-level spec accessors, meaning that all named fields should match,
 *    as well as all array accesses should be within the actual bounds. Also,
 *    types should be compatible (see bpf_core_fields_are_compat for details).
 * 3. It is supported and expected that there might be multiple flavors
 *    matching the spec. As long as all the specs resolve to the same set of
 *    offsets across all candidates, there is no error. If there is any
 *    ambiguity, CO-RE relocation will fail. This is necessary to accomodate
 *    imprefection of BTF deduplication, which can cause slight duplication of
 *    the same BTF type, if some directly or indirectly referenced (by
 *    pointer) type gets resolved to different actual types in different
 *    object files. If such situation occurs, deduplicated BTF will end up
 *    with two (or more) structurally identical types, which differ only in
 *    types they refer to through pointer. This should be OK in most cases and
 *    is not an error.
 * 4. Candidate types search is performed by linearly scanning through all
 *    types in target BTF. It is anticipated that this is overall more
 *    efficient memory-wise and not significantly worse (if not better)
 *    CPU-wise compared to prebuilding a map from all local type names to
 *    a list of candidate type names. It's also sped up by caching resolved
 *    list of matching candidates per each local "root" type ID, that has at
 *    least one bpf_field_reloc associated with it. This list is shared
 *    between multiple relocations for the same type ID and is updated as some
 *    of the candidates are pruned due to structural incompatibility.
 */
static int bpf_core_reloc_field(struct bpf_program *prog,
				 const struct bpf_field_reloc *relo,
				 int relo_idx,
				 const struct btf *local_btf,
				 const struct btf *targ_btf,
				 struct hashmap *cand_cache)
{
	const char *prog_name = bpf_program__title(prog, false);
	struct bpf_core_spec local_spec, cand_spec, targ_spec;
	const void *type_key = u32_as_hash_key(relo->type_id);
	const struct btf_type *local_type, *cand_type;
	const char *local_name, *cand_name;
	struct ids_vec *cand_ids;
	__u32 local_id, cand_id;
	const char *spec_str;
	int i, j, err;

	local_id = relo->type_id;
	local_type = btf__type_by_id(local_btf, local_id);
	if (!local_type)
		return -EINVAL;

	local_name = btf__name_by_offset(local_btf, local_type->name_off);
	if (str_is_empty(local_name))
		return -EINVAL;

	spec_str = btf__name_by_offset(local_btf, relo->access_str_off);
	if (str_is_empty(spec_str))
		return -EINVAL;

	err = bpf_core_spec_parse(local_btf, local_id, spec_str, &local_spec);
	if (err) {
		pr_warn("prog '%s': relo #%d: parsing [%d] %s + %s failed: %d\n",
			prog_name, relo_idx, local_id, local_name, spec_str,
			err);
		return -EINVAL;
	}

	pr_debug("prog '%s': relo #%d: kind %d, spec is ", prog_name, relo_idx,
		 relo->kind);
	bpf_core_dump_spec(LIBBPF_DEBUG, &local_spec);
	libbpf_print(LIBBPF_DEBUG, "\n");

	if (!hashmap__find(cand_cache, type_key, (void **)&cand_ids)) {
		cand_ids = bpf_core_find_cands(local_btf, local_id, targ_btf);
		if (IS_ERR(cand_ids)) {
			pr_warn("prog '%s': relo #%d: target candidate search failed for [%d] %s: %ld",
				prog_name, relo_idx, local_id, local_name,
				PTR_ERR(cand_ids));
			return PTR_ERR(cand_ids);
		}
		err = hashmap__set(cand_cache, type_key, cand_ids, NULL, NULL);
		if (err) {
			bpf_core_free_cands(cand_ids);
			return err;
		}
	}

	for (i = 0, j = 0; i < cand_ids->len; i++) {
		cand_id = cand_ids->data[i];
		cand_type = btf__type_by_id(targ_btf, cand_id);
		cand_name = btf__name_by_offset(targ_btf, cand_type->name_off);

		err = bpf_core_spec_match(&local_spec, targ_btf,
					  cand_id, &cand_spec);
		pr_debug("prog '%s': relo #%d: matching candidate #%d %s against spec ",
			 prog_name, relo_idx, i, cand_name);
		bpf_core_dump_spec(LIBBPF_DEBUG, &cand_spec);
		libbpf_print(LIBBPF_DEBUG, ": %d\n", err);
		if (err < 0) {
			pr_warn("prog '%s': relo #%d: matching error: %d\n",
				prog_name, relo_idx, err);
			return err;
		}
		if (err == 0)
			continue;

		if (j == 0) {
			targ_spec = cand_spec;
		} else if (cand_spec.bit_offset != targ_spec.bit_offset) {
			/* if there are many candidates, they should all
			 * resolve to the same bit offset
			 */
			pr_warn("prog '%s': relo #%d: offset ambiguity: %u != %u\n",
				prog_name, relo_idx, cand_spec.bit_offset,
				targ_spec.bit_offset);
			return -EINVAL;
		}

		cand_ids->data[j++] = cand_spec.spec[0].type_id;
	}

	/*
	 * For BPF_FIELD_EXISTS relo or when used BPF program has field
	 * existence checks or kernel version/config checks, it's expected
	 * that we might not find any candidates. In this case, if field
	 * wasn't found in any candidate, the list of candidates shouldn't
	 * change at all, we'll just handle relocating appropriately,
	 * depending on relo's kind.
	 */
	if (j > 0)
		cand_ids->len = j;

	/*
	 * If no candidates were found, it might be both a programmer error,
	 * as well as expected case, depending whether instruction w/
	 * relocation is guarded in some way that makes it unreachable (dead
	 * code) if relocation can't be resolved. This is handled in
	 * bpf_core_reloc_insn() uniformly by replacing that instruction with
	 * BPF helper call insn (using invalid helper ID). If that instruction
	 * is indeed unreachable, then it will be ignored and eliminated by
	 * verifier. If it was an error, then verifier will complain and point
	 * to a specific instruction number in its log.
	 */
	if (j == 0)
		pr_debug("prog '%s': relo #%d: no matching targets found for [%d] %s + %s\n",
			 prog_name, relo_idx, local_id, local_name, spec_str);

	/* bpf_core_reloc_insn should know how to handle missing targ_spec */
	err = bpf_core_reloc_insn(prog, relo, relo_idx, &local_spec,
				  j ? &targ_spec : NULL);
	if (err) {
		pr_warn("prog '%s': relo #%d: failed to patch insn at offset %d: %d\n",
			prog_name, relo_idx, relo->insn_off, err);
		return -EINVAL;
	}

	return 0;
}

static int
bpf_core_reloc_fields(struct bpf_object *obj, const char *targ_btf_path)
{
	const struct btf_ext_info_sec *sec;
	const struct bpf_field_reloc *rec;
	const struct btf_ext_info *seg;
	struct hashmap_entry *entry;
	struct hashmap *cand_cache = NULL;
	struct bpf_program *prog;
	struct btf *targ_btf;
	const char *sec_name;
	int i, err = 0;

	if (targ_btf_path)
		targ_btf = btf__parse_elf(targ_btf_path, NULL);
	else
		targ_btf = libbpf_find_kernel_btf();
	if (IS_ERR(targ_btf)) {
		pr_warn("failed to get target BTF: %ld\n", PTR_ERR(targ_btf));
		return PTR_ERR(targ_btf);
	}

	cand_cache = hashmap__new(bpf_core_hash_fn, bpf_core_equal_fn, NULL);
	if (IS_ERR(cand_cache)) {
		err = PTR_ERR(cand_cache);
		goto out;
	}

	seg = &obj->btf_ext->field_reloc_info;
	for_each_btf_ext_sec(seg, sec) {
		sec_name = btf__name_by_offset(obj->btf, sec->sec_name_off);
		if (str_is_empty(sec_name)) {
			err = -EINVAL;
			goto out;
		}
		prog = bpf_object__find_program_by_title(obj, sec_name);
		if (!prog) {
			pr_warn("failed to find program '%s' for CO-RE offset relocation\n",
				sec_name);
			err = -EINVAL;
			goto out;
		}

		pr_debug("prog '%s': performing %d CO-RE offset relocs\n",
			 sec_name, sec->num_info);

		for_each_btf_ext_rec(seg, sec, i, rec) {
			err = bpf_core_reloc_field(prog, rec, i, obj->btf,
						   targ_btf, cand_cache);
			if (err) {
				pr_warn("prog '%s': relo #%d: failed to relocate: %d\n",
					sec_name, i, err);
				goto out;
			}
		}
	}

out:
	btf__free(targ_btf);
	if (!IS_ERR_OR_NULL(cand_cache)) {
		hashmap__for_each_entry(cand_cache, entry, i) {
			bpf_core_free_cands(entry->value);
		}
		hashmap__free(cand_cache);
	}
	return err;
}

static int
bpf_object__relocate_core(struct bpf_object *obj, const char *targ_btf_path)
{
	int err = 0;

	if (obj->btf_ext->field_reloc_info.len)
		err = bpf_core_reloc_fields(obj, targ_btf_path);

	return err;
}

static int
bpf_program__reloc_text(struct bpf_program *prog, struct bpf_object *obj,
			struct reloc_desc *relo)
{
	struct bpf_insn *insn, *new_insn;
	struct bpf_program *text;
	size_t new_cnt;
	int err;

	if (prog->idx != obj->efile.text_shndx && prog->main_prog_cnt == 0) {
		text = bpf_object__find_prog_by_idx(obj, obj->efile.text_shndx);
		if (!text) {
			pr_warn("no .text section found yet relo into text exist\n");
			return -LIBBPF_ERRNO__RELOC;
		}
		new_cnt = prog->insns_cnt + text->insns_cnt;
		new_insn = reallocarray(prog->insns, new_cnt, sizeof(*insn));
		if (!new_insn) {
			pr_warn("oom in prog realloc\n");
			return -ENOMEM;
		}
		prog->insns = new_insn;

		if (obj->btf_ext) {
			err = bpf_program_reloc_btf_ext(prog, obj,
							text->section_name,
							prog->insns_cnt);
			if (err)
				return err;
		}

		memcpy(new_insn + prog->insns_cnt, text->insns,
		       text->insns_cnt * sizeof(*insn));
		prog->main_prog_cnt = prog->insns_cnt;
		prog->insns_cnt = new_cnt;
		pr_debug("added %zd insn from %s to prog %s\n",
			 text->insns_cnt, text->section_name,
			 prog->section_name);
	}

	insn = &prog->insns[relo->insn_idx];
	insn->imm += relo->sym_off / 8 + prog->main_prog_cnt - relo->insn_idx;
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
		struct reloc_desc *relo = &prog->reloc_desc[i];
		struct bpf_insn *insn = &prog->insns[relo->insn_idx];

		if (relo->insn_idx + 1 >= (int)prog->insns_cnt) {
			pr_warn("relocation out of range: '%s'\n",
				prog->section_name);
			return -LIBBPF_ERRNO__RELOC;
		}

		switch (relo->type) {
		case RELO_LD64:
			insn[0].src_reg = BPF_PSEUDO_MAP_FD;
			insn[0].imm = obj->maps[relo->map_idx].fd;
			break;
		case RELO_DATA:
			insn[0].src_reg = BPF_PSEUDO_MAP_VALUE;
			insn[1].imm = insn[0].imm + relo->sym_off;
			insn[0].imm = obj->maps[relo->map_idx].fd;
			break;
		case RELO_EXTERN:
			insn[0].src_reg = BPF_PSEUDO_MAP_VALUE;
			insn[0].imm = obj->maps[obj->kconfig_map_idx].fd;
			insn[1].imm = relo->sym_off;
			break;
		case RELO_CALL:
			err = bpf_program__reloc_text(prog, obj, relo);
			if (err)
				return err;
			break;
		default:
			pr_warn("relo #%d: bad relo type %d\n", i, relo->type);
			return -EINVAL;
		}
	}

	zfree(&prog->reloc_desc);
	prog->nr_reloc = 0;
	return 0;
}

static int
bpf_object__relocate(struct bpf_object *obj, const char *targ_btf_path)
{
	struct bpf_program *prog;
	size_t i;
	int err;

	if (obj->btf_ext) {
		err = bpf_object__relocate_core(obj, targ_btf_path);
		if (err) {
			pr_warn("failed to perform CO-RE relocations: %d\n",
				err);
			return err;
		}
	}
	/* ensure .text is relocated first, as it's going to be copied as-is
	 * later for sub-program calls
	 */
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog->idx != obj->efile.text_shndx)
			continue;

		err = bpf_program__relocate(prog, obj);
		if (err) {
			pr_warn("failed to relocate '%s'\n", prog->section_name);
			return err;
		}
		break;
	}
	/* now relocate everything but .text, which by now is relocated
	 * properly, so we can copy raw sub-program instructions as is safely
	 */
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog->idx == obj->efile.text_shndx)
			continue;

		err = bpf_program__relocate(prog, obj);
		if (err) {
			pr_warn("failed to relocate '%s'\n", prog->section_name);
			return err;
		}
	}
	return 0;
}

static int bpf_object__collect_struct_ops_map_reloc(struct bpf_object *obj,
						    GElf_Shdr *shdr,
						    Elf_Data *data);

static int bpf_object__collect_reloc(struct bpf_object *obj)
{
	int i, err;

	if (!obj_elf_valid(obj)) {
		pr_warn("Internal error: elf object is closed\n");
		return -LIBBPF_ERRNO__INTERNAL;
	}

	for (i = 0; i < obj->efile.nr_reloc_sects; i++) {
		GElf_Shdr *shdr = &obj->efile.reloc_sects[i].shdr;
		Elf_Data *data = obj->efile.reloc_sects[i].data;
		int idx = shdr->sh_info;
		struct bpf_program *prog;

		if (shdr->sh_type != SHT_REL) {
			pr_warn("internal error at %d\n", __LINE__);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		if (idx == obj->efile.st_ops_shndx) {
			err = bpf_object__collect_struct_ops_map_reloc(obj,
								       shdr,
								       data);
			if (err)
				return err;
			continue;
		}

		prog = bpf_object__find_prog_by_idx(obj, idx);
		if (!prog) {
			pr_warn("relocation failed: no section(%d)\n", idx);
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
	if (prog->type == BPF_PROG_TYPE_STRUCT_OPS) {
		load_attr.attach_btf_id = prog->attach_btf_id;
	} else if (prog->type == BPF_PROG_TYPE_TRACING ||
		   prog->type == BPF_PROG_TYPE_EXT) {
		load_attr.attach_prog_fd = prog->attach_prog_fd;
		load_attr.attach_btf_id = prog->attach_btf_id;
	} else {
		load_attr.kern_version = kern_version;
		load_attr.prog_ifindex = prog->prog_ifindex;
	}
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
		pr_warn("Alloc log buffer for bpf loader error, continue without log\n");

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
	ret = -errno;
	cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
	pr_warn("load bpf program failed: %s\n", cp);
	pr_perm_msg(ret);

	if (log_buf && log_buf[0] != '\0') {
		ret = -LIBBPF_ERRNO__VERIFY;
		pr_warn("-- BEGIN DUMP LOG ---\n");
		pr_warn("\n%s\n", log_buf);
		pr_warn("-- END LOG --\n");
	} else if (load_attr.insns_cnt >= BPF_MAXINSNS) {
		pr_warn("Program too large (%zu insns), at most %d insns\n",
			load_attr.insns_cnt, BPF_MAXINSNS);
		ret = -LIBBPF_ERRNO__PROG2BIG;
	} else if (load_attr.prog_type != BPF_PROG_TYPE_KPROBE) {
		/* Wrong program type? */
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

out:
	free(log_buf);
	return ret;
}

static int libbpf_find_attach_btf_id(struct bpf_program *prog);

int bpf_program__load(struct bpf_program *prog, char *license, __u32 kern_ver)
{
	int err = 0, fd, i, btf_id;

	if ((prog->type == BPF_PROG_TYPE_TRACING ||
	     prog->type == BPF_PROG_TYPE_EXT) && !prog->attach_btf_id) {
		btf_id = libbpf_find_attach_btf_id(prog);
		if (btf_id <= 0)
			return btf_id;
		prog->attach_btf_id = btf_id;
	}

	if (prog->instances.nr < 0 || !prog->instances.fds) {
		if (prog->preprocessor) {
			pr_warn("Internal error: can't load program '%s'\n",
				prog->section_name);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		prog->instances.fds = malloc(sizeof(int));
		if (!prog->instances.fds) {
			pr_warn("Not enough memory for BPF fds\n");
			return -ENOMEM;
		}
		prog->instances.nr = 1;
		prog->instances.fds[0] = -1;
	}

	if (!prog->preprocessor) {
		if (prog->instances.nr != 1) {
			pr_warn("Program '%s' is inconsistent: nr(%d) != 1\n",
				prog->section_name, prog->instances.nr);
		}
		err = load_program(prog, prog->insns, prog->insns_cnt,
				   license, kern_ver, &fd);
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
			pr_warn("Preprocessing the %dth instance of program '%s' failed\n",
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
				   result.new_insn_cnt, license, kern_ver, &fd);
		if (err) {
			pr_warn("Loading the %dth instance of program '%s' failed\n",
				i, prog->section_name);
			goto out;
		}

		if (result.pfd)
			*result.pfd = fd;
		prog->instances.fds[i] = fd;
	}
out:
	if (err)
		pr_warn("failed to load program '%s'\n", prog->section_name);
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

static struct bpf_object *
__bpf_object__open(const char *path, const void *obj_buf, size_t obj_buf_sz,
		   const struct bpf_object_open_opts *opts)
{
	const char *obj_name, *kconfig;
	struct bpf_program *prog;
	struct bpf_object *obj;
	char tmp_name[64];
	int err;

	if (elf_version(EV_CURRENT) == EV_NONE) {
		pr_warn("failed to init libelf for %s\n",
			path ? : "(mem buf)");
		return ERR_PTR(-LIBBPF_ERRNO__LIBELF);
	}

	if (!OPTS_VALID(opts, bpf_object_open_opts))
		return ERR_PTR(-EINVAL);

	obj_name = OPTS_GET(opts, object_name, NULL);
	if (obj_buf) {
		if (!obj_name) {
			snprintf(tmp_name, sizeof(tmp_name), "%lx-%lx",
				 (unsigned long)obj_buf,
				 (unsigned long)obj_buf_sz);
			obj_name = tmp_name;
		}
		path = obj_name;
		pr_debug("loading object '%s' from buffer\n", obj_name);
	}

	obj = bpf_object__new(path, obj_buf, obj_buf_sz, obj_name);
	if (IS_ERR(obj))
		return obj;

	kconfig = OPTS_GET(opts, kconfig, NULL);
	if (kconfig) {
		obj->kconfig = strdup(kconfig);
		if (!obj->kconfig)
			return ERR_PTR(-ENOMEM);
	}

	err = bpf_object__elf_init(obj);
	err = err ? : bpf_object__check_endianness(obj);
	err = err ? : bpf_object__elf_collect(obj);
	err = err ? : bpf_object__collect_externs(obj);
	err = err ? : bpf_object__finalize_btf(obj);
	err = err ? : bpf_object__init_maps(obj, opts);
	err = err ? : bpf_object__init_prog_names(obj);
	err = err ? : bpf_object__collect_reloc(obj);
	if (err)
		goto out;
	bpf_object__elf_finish(obj);

	bpf_object__for_each_program(prog, obj) {
		enum bpf_prog_type prog_type;
		enum bpf_attach_type attach_type;

		if (prog->type != BPF_PROG_TYPE_UNSPEC)
			continue;

		err = libbpf_prog_type_by_name(prog->section_name, &prog_type,
					       &attach_type);
		if (err == -ESRCH)
			/* couldn't guess, but user might manually specify */
			continue;
		if (err)
			goto out;

		bpf_program__set_type(prog, prog_type);
		bpf_program__set_expected_attach_type(prog, attach_type);
		if (prog_type == BPF_PROG_TYPE_TRACING ||
		    prog_type == BPF_PROG_TYPE_EXT)
			prog->attach_prog_fd = OPTS_GET(opts, attach_prog_fd, 0);
	}

	return obj;
out:
	bpf_object__close(obj);
	return ERR_PTR(err);
}

static struct bpf_object *
__bpf_object__open_xattr(struct bpf_object_open_attr *attr, int flags)
{
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
		.relaxed_maps = flags & MAPS_RELAX_COMPAT,
	);

	/* param validation */
	if (!attr->file)
		return NULL;

	pr_debug("loading %s\n", attr->file);
	return __bpf_object__open(attr->file, NULL, 0, &opts);
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

struct bpf_object *
bpf_object__open_file(const char *path, const struct bpf_object_open_opts *opts)
{
	if (!path)
		return ERR_PTR(-EINVAL);

	pr_debug("loading %s\n", path);

	return __bpf_object__open(path, NULL, 0, opts);
}

struct bpf_object *
bpf_object__open_mem(const void *obj_buf, size_t obj_buf_sz,
		     const struct bpf_object_open_opts *opts)
{
	if (!obj_buf || obj_buf_sz == 0)
		return ERR_PTR(-EINVAL);

	return __bpf_object__open(NULL, obj_buf, obj_buf_sz, opts);
}

struct bpf_object *
bpf_object__open_buffer(const void *obj_buf, size_t obj_buf_sz,
			const char *name)
{
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, opts,
		.object_name = name,
		/* wrong default, but backwards-compatible */
		.relaxed_maps = true,
	);

	/* returning NULL is wrong, but backwards-compatible */
	if (!obj_buf || obj_buf_sz == 0)
		return NULL;

	return bpf_object__open_mem(obj_buf, obj_buf_sz, &opts);
}

int bpf_object__unload(struct bpf_object *obj)
{
	size_t i;

	if (!obj)
		return -EINVAL;

	for (i = 0; i < obj->nr_maps; i++) {
		zclose(obj->maps[i].fd);
		if (obj->maps[i].st_ops)
			zfree(&obj->maps[i].st_ops->kern_vdata);
	}

	for (i = 0; i < obj->nr_programs; i++)
		bpf_program__unload(&obj->programs[i]);

	return 0;
}

static int bpf_object__sanitize_maps(struct bpf_object *obj)
{
	struct bpf_map *m;

	bpf_object__for_each_map(m, obj) {
		if (!bpf_map__is_internal(m))
			continue;
		if (!obj->caps.global_data) {
			pr_warn("kernel doesn't support global data\n");
			return -ENOTSUP;
		}
		if (!obj->caps.array_mmap)
			m->def.map_flags ^= BPF_F_MMAPABLE;
	}

	return 0;
}

static int bpf_object__resolve_externs(struct bpf_object *obj,
				       const char *extra_kconfig)
{
	bool need_config = false;
	struct extern_desc *ext;
	int err, i;
	void *data;

	if (obj->nr_extern == 0)
		return 0;

	data = obj->maps[obj->kconfig_map_idx].mmaped;

	for (i = 0; i < obj->nr_extern; i++) {
		ext = &obj->externs[i];

		if (strcmp(ext->name, "LINUX_KERNEL_VERSION") == 0) {
			void *ext_val = data + ext->data_off;
			__u32 kver = get_kernel_version();

			if (!kver) {
				pr_warn("failed to get kernel version\n");
				return -EINVAL;
			}
			err = set_ext_value_num(ext, ext_val, kver);
			if (err)
				return err;
			pr_debug("extern %s=0x%x\n", ext->name, kver);
		} else if (strncmp(ext->name, "CONFIG_", 7) == 0) {
			need_config = true;
		} else {
			pr_warn("unrecognized extern '%s'\n", ext->name);
			return -EINVAL;
		}
	}
	if (need_config && extra_kconfig) {
		err = bpf_object__read_kconfig_mem(obj, extra_kconfig, data);
		if (err)
			return -EINVAL;
		need_config = false;
		for (i = 0; i < obj->nr_extern; i++) {
			ext = &obj->externs[i];
			if (!ext->is_set) {
				need_config = true;
				break;
			}
		}
	}
	if (need_config) {
		err = bpf_object__read_kconfig_file(obj, data);
		if (err)
			return -EINVAL;
	}
	for (i = 0; i < obj->nr_extern; i++) {
		ext = &obj->externs[i];

		if (!ext->is_set && !ext->is_weak) {
			pr_warn("extern %s (strong) not resolved\n", ext->name);
			return -ESRCH;
		} else if (!ext->is_set) {
			pr_debug("extern %s (weak) not resolved, defaulting to zero\n",
				 ext->name);
		}
	}

	return 0;
}

int bpf_object__load_xattr(struct bpf_object_load_attr *attr)
{
	struct bpf_object *obj;
	int err, i;

	if (!attr)
		return -EINVAL;
	obj = attr->obj;
	if (!obj)
		return -EINVAL;

	if (obj->loaded) {
		pr_warn("object should not be loaded twice\n");
		return -EINVAL;
	}

	obj->loaded = true;

	err = bpf_object__probe_caps(obj);
	err = err ? : bpf_object__resolve_externs(obj, obj->kconfig);
	err = err ? : bpf_object__sanitize_and_load_btf(obj);
	err = err ? : bpf_object__sanitize_maps(obj);
	err = err ? : bpf_object__load_vmlinux_btf(obj);
	err = err ? : bpf_object__init_kern_struct_ops_maps(obj);
	err = err ? : bpf_object__create_maps(obj);
	err = err ? : bpf_object__relocate(obj, attr->target_btf_path);
	err = err ? : bpf_object__load_progs(obj, attr->log_level);

	btf__free(obj->btf_vmlinux);
	obj->btf_vmlinux = NULL;

	if (err)
		goto out;

	return 0;
out:
	/* unpin any maps that were auto-pinned during load */
	for (i = 0; i < obj->nr_maps; i++)
		if (obj->maps[i].pinned && !obj->maps[i].reused)
			bpf_map__unpin(&obj->maps[i], NULL);

	bpf_object__unload(obj);
	pr_warn("failed to load object '%s'\n", obj->path);
	return err;
}

int bpf_object__load(struct bpf_object *obj)
{
	struct bpf_object_load_attr attr = {
		.obj = obj,
	};

	return bpf_object__load_xattr(&attr);
}

static int make_parent_dir(const char *path)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	char *dname, *dir;
	int err = 0;

	dname = strdup(path);
	if (dname == NULL)
		return -ENOMEM;

	dir = dirname(dname);
	if (mkdir(dir, 0700) && errno != EEXIST)
		err = -errno;

	free(dname);
	if (err) {
		cp = libbpf_strerror_r(-err, errmsg, sizeof(errmsg));
		pr_warn("failed to mkdir %s: %s\n", path, cp);
	}
	return err;
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
		pr_warn("failed to statfs %s: %s\n", dir, cp);
		err = -errno;
	}
	free(dname);

	if (!err && st_fs.f_type != BPF_FS_MAGIC) {
		pr_warn("specified path %s is not on BPF FS\n", path);
		err = -EINVAL;
	}

	return err;
}

int bpf_program__pin_instance(struct bpf_program *prog, const char *path,
			      int instance)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err;

	err = make_parent_dir(path);
	if (err)
		return err;

	err = check_path(path);
	if (err)
		return err;

	if (prog == NULL) {
		pr_warn("invalid program pointer\n");
		return -EINVAL;
	}

	if (instance < 0 || instance >= prog->instances.nr) {
		pr_warn("invalid prog instance %d of prog %s (max %d)\n",
			instance, prog->section_name, prog->instances.nr);
		return -EINVAL;
	}

	if (bpf_obj_pin(prog->instances.fds[instance], path)) {
		cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
		pr_warn("failed to pin program: %s\n", cp);
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
		pr_warn("invalid program pointer\n");
		return -EINVAL;
	}

	if (instance < 0 || instance >= prog->instances.nr) {
		pr_warn("invalid prog instance %d of prog %s (max %d)\n",
			instance, prog->section_name, prog->instances.nr);
		return -EINVAL;
	}

	err = unlink(path);
	if (err != 0)
		return -errno;
	pr_debug("unpinned program '%s'\n", path);

	return 0;
}

int bpf_program__pin(struct bpf_program *prog, const char *path)
{
	int i, err;

	err = make_parent_dir(path);
	if (err)
		return err;

	err = check_path(path);
	if (err)
		return err;

	if (prog == NULL) {
		pr_warn("invalid program pointer\n");
		return -EINVAL;
	}

	if (prog->instances.nr <= 0) {
		pr_warn("no instances of prog %s to pin\n",
			   prog->section_name);
		return -EINVAL;
	}

	if (prog->instances.nr == 1) {
		/* don't create subdirs when pinning single instance */
		return bpf_program__pin_instance(prog, path, 0);
	}

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
		pr_warn("invalid program pointer\n");
		return -EINVAL;
	}

	if (prog->instances.nr <= 0) {
		pr_warn("no instances of prog %s to pin\n",
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

	if (map == NULL) {
		pr_warn("invalid map pointer\n");
		return -EINVAL;
	}

	if (map->pin_path) {
		if (path && strcmp(path, map->pin_path)) {
			pr_warn("map '%s' already has pin path '%s' different from '%s'\n",
				bpf_map__name(map), map->pin_path, path);
			return -EINVAL;
		} else if (map->pinned) {
			pr_debug("map '%s' already pinned at '%s'; not re-pinning\n",
				 bpf_map__name(map), map->pin_path);
			return 0;
		}
	} else {
		if (!path) {
			pr_warn("missing a path to pin map '%s' at\n",
				bpf_map__name(map));
			return -EINVAL;
		} else if (map->pinned) {
			pr_warn("map '%s' already pinned\n", bpf_map__name(map));
			return -EEXIST;
		}

		map->pin_path = strdup(path);
		if (!map->pin_path) {
			err = -errno;
			goto out_err;
		}
	}

	err = make_parent_dir(map->pin_path);
	if (err)
		return err;

	err = check_path(map->pin_path);
	if (err)
		return err;

	if (bpf_obj_pin(map->fd, map->pin_path)) {
		err = -errno;
		goto out_err;
	}

	map->pinned = true;
	pr_debug("pinned map '%s'\n", map->pin_path);

	return 0;

out_err:
	cp = libbpf_strerror_r(-err, errmsg, sizeof(errmsg));
	pr_warn("failed to pin map: %s\n", cp);
	return err;
}

int bpf_map__unpin(struct bpf_map *map, const char *path)
{
	int err;

	if (map == NULL) {
		pr_warn("invalid map pointer\n");
		return -EINVAL;
	}

	if (map->pin_path) {
		if (path && strcmp(path, map->pin_path)) {
			pr_warn("map '%s' already has pin path '%s' different from '%s'\n",
				bpf_map__name(map), map->pin_path, path);
			return -EINVAL;
		}
		path = map->pin_path;
	} else if (!path) {
		pr_warn("no path to unpin map '%s' from\n",
			bpf_map__name(map));
		return -EINVAL;
	}

	err = check_path(path);
	if (err)
		return err;

	err = unlink(path);
	if (err != 0)
		return -errno;

	map->pinned = false;
	pr_debug("unpinned map '%s' from '%s'\n", bpf_map__name(map), path);

	return 0;
}

int bpf_map__set_pin_path(struct bpf_map *map, const char *path)
{
	char *new = NULL;

	if (path) {
		new = strdup(path);
		if (!new)
			return -errno;
	}

	free(map->pin_path);
	map->pin_path = new;
	return 0;
}

const char *bpf_map__get_pin_path(const struct bpf_map *map)
{
	return map->pin_path;
}

bool bpf_map__is_pinned(const struct bpf_map *map)
{
	return map->pinned;
}

int bpf_object__pin_maps(struct bpf_object *obj, const char *path)
{
	struct bpf_map *map;
	int err;

	if (!obj)
		return -ENOENT;

	if (!obj->loaded) {
		pr_warn("object not yet loaded; load it first\n");
		return -ENOENT;
	}

	bpf_object__for_each_map(map, obj) {
		char *pin_path = NULL;
		char buf[PATH_MAX];

		if (path) {
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
			pin_path = buf;
		} else if (!map->pin_path) {
			continue;
		}

		err = bpf_map__pin(map, pin_path);
		if (err)
			goto err_unpin_maps;
	}

	return 0;

err_unpin_maps:
	while ((map = bpf_map__prev(map, obj))) {
		if (!map->pin_path)
			continue;

		bpf_map__unpin(map, NULL);
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
		char *pin_path = NULL;
		char buf[PATH_MAX];

		if (path) {
			int len;

			len = snprintf(buf, PATH_MAX, "%s/%s", path,
				       bpf_map__name(map));
			if (len < 0)
				return -EINVAL;
			else if (len >= PATH_MAX)
				return -ENAMETOOLONG;
			pin_path = buf;
		} else if (!map->pin_path) {
			continue;
		}

		err = bpf_map__unpin(map, pin_path);
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
		pr_warn("object not yet loaded; load it first\n");
		return -ENOENT;
	}

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
		struct bpf_map *map = &obj->maps[i];

		if (map->clear_priv)
			map->clear_priv(map, map->priv);
		map->priv = NULL;
		map->clear_priv = NULL;

		if (map->mmaped) {
			munmap(map->mmaped, bpf_map_mmap_sz(map));
			map->mmaped = NULL;
		}

		if (map->st_ops) {
			zfree(&map->st_ops->data);
			zfree(&map->st_ops->progs);
			zfree(&map->st_ops->kern_func_off);
			zfree(&map->st_ops);
		}

		zfree(&map->name);
		zfree(&map->pin_path);
	}

	zfree(&obj->kconfig);
	zfree(&obj->externs);
	obj->nr_extern = 0;

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
	return obj ? obj->name : ERR_PTR(-EINVAL);
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
		pr_warn("error: program handler doesn't match object\n");
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

const char *bpf_program__name(const struct bpf_program *prog)
{
	return prog->name;
}

const char *bpf_program__title(const struct bpf_program *prog, bool needs_copy)
{
	const char *title;

	title = prog->section_name;
	if (needs_copy) {
		title = strdup(title);
		if (!title) {
			pr_warn("failed to strdup program title\n");
			return ERR_PTR(-ENOMEM);
		}
	}

	return title;
}

int bpf_program__fd(const struct bpf_program *prog)
{
	return bpf_program__nth_fd(prog, 0);
}

size_t bpf_program__size(const struct bpf_program *prog)
{
	return prog->insns_cnt * sizeof(struct bpf_insn);
}

int bpf_program__set_prep(struct bpf_program *prog, int nr_instances,
			  bpf_program_prep_t prep)
{
	int *instances_fds;

	if (nr_instances <= 0 || !prep)
		return -EINVAL;

	if (prog->instances.nr > 0 || prog->instances.fds) {
		pr_warn("Can't set pre-processor after loading\n");
		return -EINVAL;
	}

	instances_fds = malloc(sizeof(int) * nr_instances);
	if (!instances_fds) {
		pr_warn("alloc memory failed for fds\n");
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
		pr_warn("Can't get the %dth fd from program %s: only %d instances\n",
			n, prog->section_name, prog->instances.nr);
		return -EINVAL;
	}

	fd = prog->instances.fds[n];
	if (fd < 0) {
		pr_warn("%dth instance of program '%s' is invalid\n",
			n, prog->section_name);
		return -ENOENT;
	}

	return fd;
}

enum bpf_prog_type bpf_program__get_type(struct bpf_program *prog)
{
	return prog->type;
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
BPF_PROG_TYPE_FNS(tracing, BPF_PROG_TYPE_TRACING);
BPF_PROG_TYPE_FNS(struct_ops, BPF_PROG_TYPE_STRUCT_OPS);
BPF_PROG_TYPE_FNS(extension, BPF_PROG_TYPE_EXT);

enum bpf_attach_type
bpf_program__get_expected_attach_type(struct bpf_program *prog)
{
	return prog->expected_attach_type;
}

void bpf_program__set_expected_attach_type(struct bpf_program *prog,
					   enum bpf_attach_type type)
{
	prog->expected_attach_type = type;
}

#define BPF_PROG_SEC_IMPL(string, ptype, eatype, is_attachable, btf, atype) \
	{ string, sizeof(string) - 1, ptype, eatype, is_attachable, btf, atype }

/* Programs that can NOT be attached. */
#define BPF_PROG_SEC(string, ptype) BPF_PROG_SEC_IMPL(string, ptype, 0, 0, 0, 0)

/* Programs that can be attached. */
#define BPF_APROG_SEC(string, ptype, atype) \
	BPF_PROG_SEC_IMPL(string, ptype, 0, 1, 0, atype)

/* Programs that must specify expected attach type at load time. */
#define BPF_EAPROG_SEC(string, ptype, eatype) \
	BPF_PROG_SEC_IMPL(string, ptype, eatype, 1, 0, eatype)

/* Programs that use BTF to identify attach point */
#define BPF_PROG_BTF(string, ptype, eatype) \
	BPF_PROG_SEC_IMPL(string, ptype, eatype, 0, 1, 0)

/* Programs that can be attached but attach type can't be identified by section
 * name. Kept for backward compatibility.
 */
#define BPF_APROG_COMPAT(string, ptype) BPF_PROG_SEC(string, ptype)

#define SEC_DEF(sec_pfx, ptype, ...) {					    \
	.sec = sec_pfx,							    \
	.len = sizeof(sec_pfx) - 1,					    \
	.prog_type = BPF_PROG_TYPE_##ptype,				    \
	__VA_ARGS__							    \
}

struct bpf_sec_def;

typedef struct bpf_link *(*attach_fn_t)(const struct bpf_sec_def *sec,
					struct bpf_program *prog);

static struct bpf_link *attach_kprobe(const struct bpf_sec_def *sec,
				      struct bpf_program *prog);
static struct bpf_link *attach_tp(const struct bpf_sec_def *sec,
				  struct bpf_program *prog);
static struct bpf_link *attach_raw_tp(const struct bpf_sec_def *sec,
				      struct bpf_program *prog);
static struct bpf_link *attach_trace(const struct bpf_sec_def *sec,
				     struct bpf_program *prog);

struct bpf_sec_def {
	const char *sec;
	size_t len;
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	bool is_attachable;
	bool is_attach_btf;
	enum bpf_attach_type attach_type;
	attach_fn_t attach_fn;
};

static const struct bpf_sec_def section_defs[] = {
	BPF_PROG_SEC("socket",			BPF_PROG_TYPE_SOCKET_FILTER),
	BPF_PROG_SEC("sk_reuseport",		BPF_PROG_TYPE_SK_REUSEPORT),
	SEC_DEF("kprobe/", KPROBE,
		.attach_fn = attach_kprobe),
	BPF_PROG_SEC("uprobe/",			BPF_PROG_TYPE_KPROBE),
	SEC_DEF("kretprobe/", KPROBE,
		.attach_fn = attach_kprobe),
	BPF_PROG_SEC("uretprobe/",		BPF_PROG_TYPE_KPROBE),
	BPF_PROG_SEC("classifier",		BPF_PROG_TYPE_SCHED_CLS),
	BPF_PROG_SEC("action",			BPF_PROG_TYPE_SCHED_ACT),
	SEC_DEF("tracepoint/", TRACEPOINT,
		.attach_fn = attach_tp),
	SEC_DEF("tp/", TRACEPOINT,
		.attach_fn = attach_tp),
	SEC_DEF("raw_tracepoint/", RAW_TRACEPOINT,
		.attach_fn = attach_raw_tp),
	SEC_DEF("raw_tp/", RAW_TRACEPOINT,
		.attach_fn = attach_raw_tp),
	SEC_DEF("tp_btf/", TRACING,
		.expected_attach_type = BPF_TRACE_RAW_TP,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
	SEC_DEF("fentry/", TRACING,
		.expected_attach_type = BPF_TRACE_FENTRY,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
	SEC_DEF("fexit/", TRACING,
		.expected_attach_type = BPF_TRACE_FEXIT,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
	SEC_DEF("freplace/", EXT,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
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
	BPF_PROG_SEC("struct_ops",		BPF_PROG_TYPE_STRUCT_OPS),
};

#undef BPF_PROG_SEC_IMPL
#undef BPF_PROG_SEC
#undef BPF_APROG_SEC
#undef BPF_EAPROG_SEC
#undef BPF_APROG_COMPAT
#undef SEC_DEF

#define MAX_TYPE_NAME_SIZE 32

static const struct bpf_sec_def *find_sec_def(const char *sec_name)
{
	int i, n = ARRAY_SIZE(section_defs);

	for (i = 0; i < n; i++) {
		if (strncmp(sec_name,
			    section_defs[i].sec, section_defs[i].len))
			continue;
		return &section_defs[i];
	}
	return NULL;
}

static char *libbpf_get_type_names(bool attach_type)
{
	int i, len = ARRAY_SIZE(section_defs) * MAX_TYPE_NAME_SIZE;
	char *buf;

	buf = malloc(len);
	if (!buf)
		return NULL;

	buf[0] = '\0';
	/* Forge string buf with all available names */
	for (i = 0; i < ARRAY_SIZE(section_defs); i++) {
		if (attach_type && !section_defs[i].is_attachable)
			continue;

		if (strlen(buf) + strlen(section_defs[i].sec) + 2 > len) {
			free(buf);
			return NULL;
		}
		strcat(buf, " ");
		strcat(buf, section_defs[i].sec);
	}

	return buf;
}

int libbpf_prog_type_by_name(const char *name, enum bpf_prog_type *prog_type,
			     enum bpf_attach_type *expected_attach_type)
{
	const struct bpf_sec_def *sec_def;
	char *type_names;

	if (!name)
		return -EINVAL;

	sec_def = find_sec_def(name);
	if (sec_def) {
		*prog_type = sec_def->prog_type;
		*expected_attach_type = sec_def->expected_attach_type;
		return 0;
	}

	pr_debug("failed to guess program type from ELF section '%s'\n", name);
	type_names = libbpf_get_type_names(false);
	if (type_names != NULL) {
		pr_debug("supported section(type) names are:%s\n", type_names);
		free(type_names);
	}

	return -ESRCH;
}

static struct bpf_map *find_struct_ops_map_by_offset(struct bpf_object *obj,
						     size_t offset)
{
	struct bpf_map *map;
	size_t i;

	for (i = 0; i < obj->nr_maps; i++) {
		map = &obj->maps[i];
		if (!bpf_map__is_struct_ops(map))
			continue;
		if (map->sec_offset <= offset &&
		    offset - map->sec_offset < map->def.value_size)
			return map;
	}

	return NULL;
}

/* Collect the reloc from ELF and populate the st_ops->progs[] */
static int bpf_object__collect_struct_ops_map_reloc(struct bpf_object *obj,
						    GElf_Shdr *shdr,
						    Elf_Data *data)
{
	const struct btf_member *member;
	struct bpf_struct_ops *st_ops;
	struct bpf_program *prog;
	unsigned int shdr_idx;
	const struct btf *btf;
	struct bpf_map *map;
	Elf_Data *symbols;
	unsigned int moff;
	const char *name;
	__u32 member_idx;
	GElf_Sym sym;
	GElf_Rel rel;
	int i, nrels;

	symbols = obj->efile.symbols;
	btf = obj->btf;
	nrels = shdr->sh_size / shdr->sh_entsize;
	for (i = 0; i < nrels; i++) {
		if (!gelf_getrel(data, i, &rel)) {
			pr_warn("struct_ops reloc: failed to get %d reloc\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (!gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym)) {
			pr_warn("struct_ops reloc: symbol %zx not found\n",
				(size_t)GELF_R_SYM(rel.r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}

		name = elf_strptr(obj->efile.elf, obj->efile.strtabidx,
				  sym.st_name) ? : "<?>";
		map = find_struct_ops_map_by_offset(obj, rel.r_offset);
		if (!map) {
			pr_warn("struct_ops reloc: cannot find map at rel.r_offset %zu\n",
				(size_t)rel.r_offset);
			return -EINVAL;
		}

		moff = rel.r_offset - map->sec_offset;
		shdr_idx = sym.st_shndx;
		st_ops = map->st_ops;
		pr_debug("struct_ops reloc %s: for %lld value %lld shdr_idx %u rel.r_offset %zu map->sec_offset %zu name %d (\'%s\')\n",
			 map->name,
			 (long long)(rel.r_info >> 32),
			 (long long)sym.st_value,
			 shdr_idx, (size_t)rel.r_offset,
			 map->sec_offset, sym.st_name, name);

		if (shdr_idx >= SHN_LORESERVE) {
			pr_warn("struct_ops reloc %s: rel.r_offset %zu shdr_idx %u unsupported non-static function\n",
				map->name, (size_t)rel.r_offset, shdr_idx);
			return -LIBBPF_ERRNO__RELOC;
		}

		member = find_member_by_offset(st_ops->type, moff * 8);
		if (!member) {
			pr_warn("struct_ops reloc %s: cannot find member at moff %u\n",
				map->name, moff);
			return -EINVAL;
		}
		member_idx = member - btf_members(st_ops->type);
		name = btf__name_by_offset(btf, member->name_off);

		if (!resolve_func_ptr(btf, member->type, NULL)) {
			pr_warn("struct_ops reloc %s: cannot relocate non func ptr %s\n",
				map->name, name);
			return -EINVAL;
		}

		prog = bpf_object__find_prog_by_idx(obj, shdr_idx);
		if (!prog) {
			pr_warn("struct_ops reloc %s: cannot find prog at shdr_idx %u to relocate func ptr %s\n",
				map->name, shdr_idx, name);
			return -EINVAL;
		}

		if (prog->type == BPF_PROG_TYPE_UNSPEC) {
			const struct bpf_sec_def *sec_def;

			sec_def = find_sec_def(prog->section_name);
			if (sec_def &&
			    sec_def->prog_type != BPF_PROG_TYPE_STRUCT_OPS) {
				/* for pr_warn */
				prog->type = sec_def->prog_type;
				goto invalid_prog;
			}

			prog->type = BPF_PROG_TYPE_STRUCT_OPS;
			prog->attach_btf_id = st_ops->type_id;
			prog->expected_attach_type = member_idx;
		} else if (prog->type != BPF_PROG_TYPE_STRUCT_OPS ||
			   prog->attach_btf_id != st_ops->type_id ||
			   prog->expected_attach_type != member_idx) {
			goto invalid_prog;
		}
		st_ops->progs[member_idx] = prog;
	}

	return 0;

invalid_prog:
	pr_warn("struct_ops reloc %s: cannot use prog %s in sec %s with type %u attach_btf_id %u expected_attach_type %u for func ptr %s\n",
		map->name, prog->name, prog->section_name, prog->type,
		prog->attach_btf_id, prog->expected_attach_type, name);
	return -EINVAL;
}

#define BTF_TRACE_PREFIX "btf_trace_"
#define BTF_MAX_NAME_SIZE 128

static int find_btf_by_prefix_kind(const struct btf *btf, const char *prefix,
				   const char *name, __u32 kind)
{
	char btf_type_name[BTF_MAX_NAME_SIZE];
	int ret;

	ret = snprintf(btf_type_name, sizeof(btf_type_name),
		       "%s%s", prefix, name);
	/* snprintf returns the number of characters written excluding the
	 * the terminating null. So, if >= BTF_MAX_NAME_SIZE are written, it
	 * indicates truncation.
	 */
	if (ret < 0 || ret >= sizeof(btf_type_name))
		return -ENAMETOOLONG;
	return btf__find_by_name_kind(btf, btf_type_name, kind);
}

static inline int __find_vmlinux_btf_id(struct btf *btf, const char *name,
					enum bpf_attach_type attach_type)
{
	int err;

	if (attach_type == BPF_TRACE_RAW_TP)
		err = find_btf_by_prefix_kind(btf, BTF_TRACE_PREFIX, name,
					      BTF_KIND_TYPEDEF);
	else
		err = btf__find_by_name_kind(btf, name, BTF_KIND_FUNC);

	if (err <= 0)
		pr_warn("%s is not found in vmlinux BTF\n", name);

	return err;
}

int libbpf_find_vmlinux_btf_id(const char *name,
			       enum bpf_attach_type attach_type)
{
	struct btf *btf;

	btf = libbpf_find_kernel_btf();
	if (IS_ERR(btf)) {
		pr_warn("vmlinux BTF is not found\n");
		return -EINVAL;
	}

	return __find_vmlinux_btf_id(btf, name, attach_type);
}

static int libbpf_find_prog_btf_id(const char *name, __u32 attach_prog_fd)
{
	struct bpf_prog_info_linear *info_linear;
	struct bpf_prog_info *info;
	struct btf *btf = NULL;
	int err = -EINVAL;

	info_linear = bpf_program__get_prog_info_linear(attach_prog_fd, 0);
	if (IS_ERR_OR_NULL(info_linear)) {
		pr_warn("failed get_prog_info_linear for FD %d\n",
			attach_prog_fd);
		return -EINVAL;
	}
	info = &info_linear->info;
	if (!info->btf_id) {
		pr_warn("The target program doesn't have BTF\n");
		goto out;
	}
	if (btf__get_from_id(info->btf_id, &btf)) {
		pr_warn("Failed to get BTF of the program\n");
		goto out;
	}
	err = btf__find_by_name_kind(btf, name, BTF_KIND_FUNC);
	btf__free(btf);
	if (err <= 0) {
		pr_warn("%s is not found in prog's BTF\n", name);
		goto out;
	}
out:
	free(info_linear);
	return err;
}

static int libbpf_find_attach_btf_id(struct bpf_program *prog)
{
	enum bpf_attach_type attach_type = prog->expected_attach_type;
	__u32 attach_prog_fd = prog->attach_prog_fd;
	const char *name = prog->section_name;
	int i, err;

	if (!name)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(section_defs); i++) {
		if (!section_defs[i].is_attach_btf)
			continue;
		if (strncmp(name, section_defs[i].sec, section_defs[i].len))
			continue;
		if (attach_prog_fd)
			err = libbpf_find_prog_btf_id(name + section_defs[i].len,
						      attach_prog_fd);
		else
			err = __find_vmlinux_btf_id(prog->obj->btf_vmlinux,
						    name + section_defs[i].len,
						    attach_type);
		return err;
	}
	pr_warn("failed to identify btf_id based on ELF section name '%s'\n", name);
	return -ESRCH;
}

int libbpf_attach_type_by_name(const char *name,
			       enum bpf_attach_type *attach_type)
{
	char *type_names;
	int i;

	if (!name)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(section_defs); i++) {
		if (strncmp(name, section_defs[i].sec, section_defs[i].len))
			continue;
		if (!section_defs[i].is_attachable)
			return -EINVAL;
		*attach_type = section_defs[i].attach_type;
		return 0;
	}
	pr_debug("failed to guess attach type based on ELF section name '%s'\n", name);
	type_names = libbpf_get_type_names(true);
	if (type_names != NULL) {
		pr_debug("attachable section(type) names are:%s\n", type_names);
		free(type_names);
	}

	return -EINVAL;
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
		pr_warn("error: unsupported map type\n");
		return -EINVAL;
	}
	if (map->inner_map_fd != -1) {
		pr_warn("error: inner_map_fd already specified\n");
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
		pr_warn("error in %s: map handler doesn't belong to object\n",
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
		enum bpf_attach_type attach_type = attr->expected_attach_type;
		/*
		 * to preserve backwards compatibility, bpf_prog_load treats
		 * attr->prog_type, if specified, as an override to whatever
		 * bpf_object__open guessed
		 */
		if (attr->prog_type != BPF_PROG_TYPE_UNSPEC) {
			bpf_program__set_type(prog, attr->prog_type);
			bpf_program__set_expected_attach_type(prog,
							      attach_type);
		}
		if (bpf_program__get_type(prog) == BPF_PROG_TYPE_UNSPEC) {
			/*
			 * we haven't guessed from section name and user
			 * didn't provide a fallback type, too bad...
			 */
			bpf_object__close(obj);
			return -EINVAL;
		}

		prog->prog_ifindex = attr->ifindex;
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
		pr_warn("object file doesn't contain bpf program\n");
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
	int (*detach)(struct bpf_link *link);
	int (*destroy)(struct bpf_link *link);
	bool disconnected;
};

/* Release "ownership" of underlying BPF resource (typically, BPF program
 * attached to some BPF hook, e.g., tracepoint, kprobe, etc). Disconnected
 * link, when destructed through bpf_link__destroy() call won't attempt to
 * detach/unregisted that BPF resource. This is useful in situations where,
 * say, attached BPF program has to outlive userspace program that attached it
 * in the system. Depending on type of BPF program, though, there might be
 * additional steps (like pinning BPF program in BPF FS) necessary to ensure
 * exit of userspace program doesn't trigger automatic detachment and clean up
 * inside the kernel.
 */
void bpf_link__disconnect(struct bpf_link *link)
{
	link->disconnected = true;
}

int bpf_link__destroy(struct bpf_link *link)
{
	int err = 0;

	if (!link)
		return 0;

	if (!link->disconnected && link->detach)
		err = link->detach(link);
	if (link->destroy)
		link->destroy(link);
	free(link);

	return err;
}

struct bpf_link_fd {
	struct bpf_link link; /* has to be at the top of struct */
	int fd; /* hook FD */
};

static int bpf_link__detach_perf_event(struct bpf_link *link)
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
		pr_warn("program '%s': invalid perf event FD %d\n",
			bpf_program__title(prog, false), pfd);
		return ERR_PTR(-EINVAL);
	}
	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("program '%s': can't attach BPF program w/o FD (did you load it?)\n",
			bpf_program__title(prog, false));
		return ERR_PTR(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return ERR_PTR(-ENOMEM);
	link->link.detach = &bpf_link__detach_perf_event;
	link->fd = pfd;

	if (ioctl(pfd, PERF_EVENT_IOC_SET_BPF, prog_fd) < 0) {
		err = -errno;
		free(link);
		pr_warn("program '%s': failed to attach to pfd %d: %s\n",
			bpf_program__title(prog, false), pfd,
			   libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return ERR_PTR(err);
	}
	if (ioctl(pfd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
		err = -errno;
		free(link);
		pr_warn("program '%s': failed to enable pfd %d: %s\n",
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
		pr_warn("failed to determine %s perf type: %s\n",
			uprobe ? "uprobe" : "kprobe",
			libbpf_strerror_r(type, errmsg, sizeof(errmsg)));
		return type;
	}
	if (retprobe) {
		int bit = uprobe ? determine_uprobe_retprobe_bit()
				 : determine_kprobe_retprobe_bit();

		if (bit < 0) {
			pr_warn("failed to determine %s retprobe bit: %s\n",
				uprobe ? "uprobe" : "kprobe",
				libbpf_strerror_r(bit, errmsg, sizeof(errmsg)));
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
		pr_warn("%s perf_event_open() failed: %s\n",
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
		pr_warn("program '%s': failed to create %s '%s' perf event: %s\n",
			bpf_program__title(prog, false),
			retprobe ? "kretprobe" : "kprobe", func_name,
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link = bpf_program__attach_perf_event(prog, pfd);
	if (IS_ERR(link)) {
		close(pfd);
		err = PTR_ERR(link);
		pr_warn("program '%s': failed to attach to %s '%s': %s\n",
			bpf_program__title(prog, false),
			retprobe ? "kretprobe" : "kprobe", func_name,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return link;
	}
	return link;
}

static struct bpf_link *attach_kprobe(const struct bpf_sec_def *sec,
				      struct bpf_program *prog)
{
	const char *func_name;
	bool retprobe;

	func_name = bpf_program__title(prog, false) + sec->len;
	retprobe = strcmp(sec->sec, "kretprobe/") == 0;

	return bpf_program__attach_kprobe(prog, retprobe, func_name);
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
		pr_warn("program '%s': failed to create %s '%s:0x%zx' perf event: %s\n",
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
		pr_warn("program '%s': failed to attach to %s '%s:0x%zx': %s\n",
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
		pr_warn("failed to determine tracepoint '%s/%s' perf event ID: %s\n",
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
		pr_warn("tracepoint '%s/%s' perf_event_open() failed: %s\n",
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
		pr_warn("program '%s': failed to create tracepoint '%s/%s' perf event: %s\n",
			bpf_program__title(prog, false),
			tp_category, tp_name,
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link = bpf_program__attach_perf_event(prog, pfd);
	if (IS_ERR(link)) {
		close(pfd);
		err = PTR_ERR(link);
		pr_warn("program '%s': failed to attach to tracepoint '%s/%s': %s\n",
			bpf_program__title(prog, false),
			tp_category, tp_name,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return link;
	}
	return link;
}

static struct bpf_link *attach_tp(const struct bpf_sec_def *sec,
				  struct bpf_program *prog)
{
	char *sec_name, *tp_cat, *tp_name;
	struct bpf_link *link;

	sec_name = strdup(bpf_program__title(prog, false));
	if (!sec_name)
		return ERR_PTR(-ENOMEM);

	/* extract "tp/<category>/<name>" */
	tp_cat = sec_name + sec->len;
	tp_name = strchr(tp_cat, '/');
	if (!tp_name) {
		link = ERR_PTR(-EINVAL);
		goto out;
	}
	*tp_name = '\0';
	tp_name++;

	link = bpf_program__attach_tracepoint(prog, tp_cat, tp_name);
out:
	free(sec_name);
	return link;
}

static int bpf_link__detach_fd(struct bpf_link *link)
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
		pr_warn("program '%s': can't attach before loaded\n",
			bpf_program__title(prog, false));
		return ERR_PTR(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return ERR_PTR(-ENOMEM);
	link->link.detach = &bpf_link__detach_fd;

	pfd = bpf_raw_tracepoint_open(tp_name, prog_fd);
	if (pfd < 0) {
		pfd = -errno;
		free(link);
		pr_warn("program '%s': failed to attach to raw tracepoint '%s': %s\n",
			bpf_program__title(prog, false), tp_name,
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link->fd = pfd;
	return (struct bpf_link *)link;
}

static struct bpf_link *attach_raw_tp(const struct bpf_sec_def *sec,
				      struct bpf_program *prog)
{
	const char *tp_name = bpf_program__title(prog, false) + sec->len;

	return bpf_program__attach_raw_tracepoint(prog, tp_name);
}

struct bpf_link *bpf_program__attach_trace(struct bpf_program *prog)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link_fd *link;
	int prog_fd, pfd;

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("program '%s': can't attach before loaded\n",
			bpf_program__title(prog, false));
		return ERR_PTR(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return ERR_PTR(-ENOMEM);
	link->link.detach = &bpf_link__detach_fd;

	pfd = bpf_raw_tracepoint_open(NULL, prog_fd);
	if (pfd < 0) {
		pfd = -errno;
		free(link);
		pr_warn("program '%s': failed to attach to trace: %s\n",
			bpf_program__title(prog, false),
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return ERR_PTR(pfd);
	}
	link->fd = pfd;
	return (struct bpf_link *)link;
}

static struct bpf_link *attach_trace(const struct bpf_sec_def *sec,
				     struct bpf_program *prog)
{
	return bpf_program__attach_trace(prog);
}

struct bpf_link *bpf_program__attach(struct bpf_program *prog)
{
	const struct bpf_sec_def *sec_def;

	sec_def = find_sec_def(bpf_program__title(prog, false));
	if (!sec_def || !sec_def->attach_fn)
		return ERR_PTR(-ESRCH);

	return sec_def->attach_fn(sec_def, prog);
}

static int bpf_link__detach_struct_ops(struct bpf_link *link)
{
	struct bpf_link_fd *l = (void *)link;
	__u32 zero = 0;

	if (bpf_map_delete_elem(l->fd, &zero))
		return -errno;

	return 0;
}

struct bpf_link *bpf_map__attach_struct_ops(struct bpf_map *map)
{
	struct bpf_struct_ops *st_ops;
	struct bpf_link_fd *link;
	__u32 i, zero = 0;
	int err;

	if (!bpf_map__is_struct_ops(map) || map->fd == -1)
		return ERR_PTR(-EINVAL);

	link = calloc(1, sizeof(*link));
	if (!link)
		return ERR_PTR(-EINVAL);

	st_ops = map->st_ops;
	for (i = 0; i < btf_vlen(st_ops->type); i++) {
		struct bpf_program *prog = st_ops->progs[i];
		void *kern_data;
		int prog_fd;

		if (!prog)
			continue;

		prog_fd = bpf_program__fd(prog);
		kern_data = st_ops->kern_vdata + st_ops->kern_func_off[i];
		*(unsigned long *)kern_data = prog_fd;
	}

	err = bpf_map_update_elem(map->fd, &zero, st_ops->kern_vdata, 0);
	if (err) {
		err = -errno;
		free(link);
		return ERR_PTR(err);
	}

	link->link.detach = bpf_link__detach_struct_ops;
	link->fd = map->fd;

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
	int cpu_cnt; /* number of allocated CPU buffers */
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
		pr_warn("failed to munmap cpu_buf #%d\n", cpu_buf->cpu);
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
		pr_warn("failed to open perf buffer event on cpu #%d: %s\n",
			cpu, libbpf_strerror_r(err, msg, sizeof(msg)));
		goto error;
	}

	cpu_buf->base = mmap(NULL, pb->mmap_size + pb->page_size,
			     PROT_READ | PROT_WRITE, MAP_SHARED,
			     cpu_buf->fd, 0);
	if (cpu_buf->base == MAP_FAILED) {
		cpu_buf->base = NULL;
		err = -errno;
		pr_warn("failed to mmap perf buffer on cpu #%d: %s\n",
			cpu, libbpf_strerror_r(err, msg, sizeof(msg)));
		goto error;
	}

	if (ioctl(cpu_buf->fd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
		err = -errno;
		pr_warn("failed to enable perf buffer event on cpu #%d: %s\n",
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
	const char *online_cpus_file = "/sys/devices/system/cpu/online";
	struct bpf_map_info map = {};
	char msg[STRERR_BUFSIZE];
	struct perf_buffer *pb;
	bool *online = NULL;
	__u32 map_info_len;
	int err, i, j, n;

	if (page_cnt & (page_cnt - 1)) {
		pr_warn("page count should be power of two, but is %zu\n",
			page_cnt);
		return ERR_PTR(-EINVAL);
	}

	map_info_len = sizeof(map);
	err = bpf_obj_get_info_by_fd(map_fd, &map, &map_info_len);
	if (err) {
		err = -errno;
		pr_warn("failed to get map info for map FD %d: %s\n",
			map_fd, libbpf_strerror_r(err, msg, sizeof(msg)));
		return ERR_PTR(err);
	}

	if (map.type != BPF_MAP_TYPE_PERF_EVENT_ARRAY) {
		pr_warn("map '%s' should be BPF_MAP_TYPE_PERF_EVENT_ARRAY\n",
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
		pr_warn("failed to create epoll instance: %s\n",
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
		pr_warn("failed to allocate events: out of memory\n");
		goto error;
	}
	pb->cpu_bufs = calloc(pb->cpu_cnt, sizeof(*pb->cpu_bufs));
	if (!pb->cpu_bufs) {
		err = -ENOMEM;
		pr_warn("failed to allocate buffers: out of memory\n");
		goto error;
	}

	err = parse_cpu_mask_file(online_cpus_file, &online, &n);
	if (err) {
		pr_warn("failed to get online CPU mask: %d\n", err);
		goto error;
	}

	for (i = 0, j = 0; i < pb->cpu_cnt; i++) {
		struct perf_cpu_buf *cpu_buf;
		int cpu, map_key;

		cpu = p->cpu_cnt > 0 ? p->cpus[i] : i;
		map_key = p->cpu_cnt > 0 ? p->map_keys[i] : i;

		/* in case user didn't explicitly requested particular CPUs to
		 * be attached to, skip offline/not present CPUs
		 */
		if (p->cpu_cnt <= 0 && (cpu >= n || !online[cpu]))
			continue;

		cpu_buf = perf_buffer__open_cpu_buf(pb, p->attr, cpu, map_key);
		if (IS_ERR(cpu_buf)) {
			err = PTR_ERR(cpu_buf);
			goto error;
		}

		pb->cpu_bufs[j] = cpu_buf;

		err = bpf_map_update_elem(pb->map_fd, &map_key,
					  &cpu_buf->fd, 0);
		if (err) {
			err = -errno;
			pr_warn("failed to set cpu #%d, key %d -> perf FD %d: %s\n",
				cpu, map_key, cpu_buf->fd,
				libbpf_strerror_r(err, msg, sizeof(msg)));
			goto error;
		}

		pb->events[j].events = EPOLLIN;
		pb->events[j].data.ptr = cpu_buf;
		if (epoll_ctl(pb->epoll_fd, EPOLL_CTL_ADD, cpu_buf->fd,
			      &pb->events[j]) < 0) {
			err = -errno;
			pr_warn("failed to epoll_ctl cpu #%d perf FD %d: %s\n",
				cpu, cpu_buf->fd,
				libbpf_strerror_r(err, msg, sizeof(msg)));
			goto error;
		}
		j++;
	}
	pb->cpu_cnt = j;
	free(online);

	return pb;

error:
	free(online);
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
		pr_warn("unknown perf sample type %d\n", e->type);
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
			pr_warn("error while processing records: %d\n", err);
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

static __u32 bpf_prog_info_read_offset_u32(struct bpf_prog_info *info,
					   int offset)
{
	__u32 *array = (__u32 *)info;

	if (offset >= 0)
		return array[offset / sizeof(__u32)];
	return -(int)offset;
}

static __u64 bpf_prog_info_read_offset_u64(struct bpf_prog_info *info,
					   int offset)
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
			pr_warn("%s: mismatch in element count\n", __func__);

		v1 = bpf_prog_info_read_offset_u32(&info, desc->size_offset);
		v2 = bpf_prog_info_read_offset_u32(&info_linear->info,
						   desc->size_offset);
		if (v1 != v2)
			pr_warn("%s: mismatch in rec size\n", __func__);
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

int bpf_program__set_attach_target(struct bpf_program *prog,
				   int attach_prog_fd,
				   const char *attach_func_name)
{
	int btf_id;

	if (!prog || attach_prog_fd < 0 || !attach_func_name)
		return -EINVAL;

	if (attach_prog_fd)
		btf_id = libbpf_find_prog_btf_id(attach_func_name,
						 attach_prog_fd);
	else
		btf_id = __find_vmlinux_btf_id(prog->obj->btf_vmlinux,
					       attach_func_name,
					       prog->expected_attach_type);

	if (btf_id < 0)
		return btf_id;

	prog->attach_btf_id = btf_id;
	prog->attach_prog_fd = attach_prog_fd;
	return 0;
}

int parse_cpu_mask_str(const char *s, bool **mask, int *mask_sz)
{
	int err = 0, n, len, start, end = -1;
	bool *tmp;

	*mask = NULL;
	*mask_sz = 0;

	/* Each sub string separated by ',' has format \d+-\d+ or \d+ */
	while (*s) {
		if (*s == ',' || *s == '\n') {
			s++;
			continue;
		}
		n = sscanf(s, "%d%n-%d%n", &start, &len, &end, &len);
		if (n <= 0 || n > 2) {
			pr_warn("Failed to get CPU range %s: %d\n", s, n);
			err = -EINVAL;
			goto cleanup;
		} else if (n == 1) {
			end = start;
		}
		if (start < 0 || start > end) {
			pr_warn("Invalid CPU range [%d,%d] in %s\n",
				start, end, s);
			err = -EINVAL;
			goto cleanup;
		}
		tmp = realloc(*mask, end + 1);
		if (!tmp) {
			err = -ENOMEM;
			goto cleanup;
		}
		*mask = tmp;
		memset(tmp + *mask_sz, 0, start - *mask_sz);
		memset(tmp + start, 1, end - start + 1);
		*mask_sz = end + 1;
		s += len;
	}
	if (!*mask_sz) {
		pr_warn("Empty CPU range\n");
		return -EINVAL;
	}
	return 0;
cleanup:
	free(*mask);
	*mask = NULL;
	return err;
}

int parse_cpu_mask_file(const char *fcpu, bool **mask, int *mask_sz)
{
	int fd, err = 0, len;
	char buf[128];

	fd = open(fcpu, O_RDONLY);
	if (fd < 0) {
		err = -errno;
		pr_warn("Failed to open cpu mask file %s: %d\n", fcpu, err);
		return err;
	}
	len = read(fd, buf, sizeof(buf));
	close(fd);
	if (len <= 0) {
		err = len ? -errno : -EINVAL;
		pr_warn("Failed to read cpu mask from %s: %d\n", fcpu, err);
		return err;
	}
	if (len >= sizeof(buf)) {
		pr_warn("CPU mask is too big in file %s\n", fcpu);
		return -E2BIG;
	}
	buf[len] = '\0';

	return parse_cpu_mask_str(buf, mask, mask_sz);
}

int libbpf_num_possible_cpus(void)
{
	static const char *fcpu = "/sys/devices/system/cpu/possible";
	static int cpus;
	int err, n, i, tmp_cpus;
	bool *mask;

	tmp_cpus = READ_ONCE(cpus);
	if (tmp_cpus > 0)
		return tmp_cpus;

	err = parse_cpu_mask_file(fcpu, &mask, &n);
	if (err)
		return err;

	tmp_cpus = 0;
	for (i = 0; i < n; i++) {
		if (mask[i])
			tmp_cpus++;
	}
	free(mask);

	WRITE_ONCE(cpus, tmp_cpus);
	return tmp_cpus;
}

int bpf_object__open_skeleton(struct bpf_object_skeleton *s,
			      const struct bpf_object_open_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, skel_opts,
		.object_name = s->name,
	);
	struct bpf_object *obj;
	int i;

	/* Attempt to preserve opts->object_name, unless overriden by user
	 * explicitly. Overwriting object name for skeletons is discouraged,
	 * as it breaks global data maps, because they contain object name
	 * prefix as their own map name prefix. When skeleton is generated,
	 * bpftool is making an assumption that this name will stay the same.
	 */
	if (opts) {
		memcpy(&skel_opts, opts, sizeof(*opts));
		if (!opts->object_name)
			skel_opts.object_name = s->name;
	}

	obj = bpf_object__open_mem(s->data, s->data_sz, &skel_opts);
	if (IS_ERR(obj)) {
		pr_warn("failed to initialize skeleton BPF object '%s': %ld\n",
			s->name, PTR_ERR(obj));
		return PTR_ERR(obj);
	}

	*s->obj = obj;

	for (i = 0; i < s->map_cnt; i++) {
		struct bpf_map **map = s->maps[i].map;
		const char *name = s->maps[i].name;
		void **mmaped = s->maps[i].mmaped;

		*map = bpf_object__find_map_by_name(obj, name);
		if (!*map) {
			pr_warn("failed to find skeleton map '%s'\n", name);
			return -ESRCH;
		}

		/* externs shouldn't be pre-setup from user code */
		if (mmaped && (*map)->libbpf_type != LIBBPF_MAP_KCONFIG)
			*mmaped = (*map)->mmaped;
	}

	for (i = 0; i < s->prog_cnt; i++) {
		struct bpf_program **prog = s->progs[i].prog;
		const char *name = s->progs[i].name;

		*prog = bpf_object__find_program_by_name(obj, name);
		if (!*prog) {
			pr_warn("failed to find skeleton program '%s'\n", name);
			return -ESRCH;
		}
	}

	return 0;
}

int bpf_object__load_skeleton(struct bpf_object_skeleton *s)
{
	int i, err;

	err = bpf_object__load(*s->obj);
	if (err) {
		pr_warn("failed to load BPF skeleton '%s': %d\n", s->name, err);
		return err;
	}

	for (i = 0; i < s->map_cnt; i++) {
		struct bpf_map *map = *s->maps[i].map;
		size_t mmap_sz = bpf_map_mmap_sz(map);
		int prot, map_fd = bpf_map__fd(map);
		void **mmaped = s->maps[i].mmaped;

		if (!mmaped)
			continue;

		if (!(map->def.map_flags & BPF_F_MMAPABLE)) {
			*mmaped = NULL;
			continue;
		}

		if (map->def.map_flags & BPF_F_RDONLY_PROG)
			prot = PROT_READ;
		else
			prot = PROT_READ | PROT_WRITE;

		/* Remap anonymous mmap()-ed "map initialization image" as
		 * a BPF map-backed mmap()-ed memory, but preserving the same
		 * memory address. This will cause kernel to change process'
		 * page table to point to a different piece of kernel memory,
		 * but from userspace point of view memory address (and its
		 * contents, being identical at this point) will stay the
		 * same. This mapping will be released by bpf_object__close()
		 * as per normal clean up procedure, so we don't need to worry
		 * about it from skeleton's clean up perspective.
		 */
		*mmaped = mmap(map->mmaped, mmap_sz, prot,
				MAP_SHARED | MAP_FIXED, map_fd, 0);
		if (*mmaped == MAP_FAILED) {
			err = -errno;
			*mmaped = NULL;
			pr_warn("failed to re-mmap() map '%s': %d\n",
				 bpf_map__name(map), err);
			return err;
		}
	}

	return 0;
}

int bpf_object__attach_skeleton(struct bpf_object_skeleton *s)
{
	int i;

	for (i = 0; i < s->prog_cnt; i++) {
		struct bpf_program *prog = *s->progs[i].prog;
		struct bpf_link **link = s->progs[i].link;
		const struct bpf_sec_def *sec_def;
		const char *sec_name = bpf_program__title(prog, false);

		sec_def = find_sec_def(sec_name);
		if (!sec_def || !sec_def->attach_fn)
			continue;

		*link = sec_def->attach_fn(sec_def, prog);
		if (IS_ERR(*link)) {
			pr_warn("failed to auto-attach program '%s': %ld\n",
				bpf_program__name(prog), PTR_ERR(*link));
			return PTR_ERR(*link);
		}
	}

	return 0;
}

void bpf_object__detach_skeleton(struct bpf_object_skeleton *s)
{
	int i;

	for (i = 0; i < s->prog_cnt; i++) {
		struct bpf_link **link = s->progs[i].link;

		if (!IS_ERR_OR_NULL(*link))
			bpf_link__destroy(*link);
		*link = NULL;
	}
}

void bpf_object__destroy_skeleton(struct bpf_object_skeleton *s)
{
	if (s->progs)
		bpf_object__detach_skeleton(s);
	if (s->obj)
		bpf_object__close(*s->obj);
	free(s->maps);
	free(s->progs);
	free(s);
}
