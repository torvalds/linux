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
#include <libelf.h>
#include <gelf.h>
#include <zlib.h>

#include "libbpf.h"
#include "bpf.h"
#include "btf.h"
#include "str_error.h"
#include "libbpf_internal.h"
#include "hashmap.h"
#include "bpf_gen_internal.h"

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC		0xcafe4a11
#endif

#define BPF_INSN_SZ (sizeof(struct bpf_insn))

/* vsprintf() in __base_pr() uses nonliteral format string. It may break
 * compilation if user enables corresponding warning. Disable it explicitly.
 */
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

#define __printf(a, b)	__attribute__((format(printf, a, b)))

static struct bpf_map *bpf_object__add_map(struct bpf_object *obj);
static bool prog_is_subprog(const struct bpf_object *obj, const struct bpf_program *prog);

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

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64) (unsigned long) ptr;
}

/* this goes away in libbpf 1.0 */
enum libbpf_strict_mode libbpf_mode = LIBBPF_STRICT_NONE;

int libbpf_set_strict_mode(enum libbpf_strict_mode mode)
{
	/* __LIBBPF_STRICT_LAST is the last power-of-2 value used + 1, so to
	 * get all possible values we compensate last +1, and then (2*x - 1)
	 * to get the bit mask
	 */
	if (mode != LIBBPF_STRICT_ALL
	    && (mode & ~((__LIBBPF_STRICT_LAST - 1) * 2 - 1)))
		return errno = EINVAL, -EINVAL;

	libbpf_mode = mode;
	return 0;
}

enum kern_feature_id {
	/* v4.14: kernel support for program & map names. */
	FEAT_PROG_NAME,
	/* v5.2: kernel support for global data sections. */
	FEAT_GLOBAL_DATA,
	/* BTF support */
	FEAT_BTF,
	/* BTF_KIND_FUNC and BTF_KIND_FUNC_PROTO support */
	FEAT_BTF_FUNC,
	/* BTF_KIND_VAR and BTF_KIND_DATASEC support */
	FEAT_BTF_DATASEC,
	/* BTF_FUNC_GLOBAL is supported */
	FEAT_BTF_GLOBAL_FUNC,
	/* BPF_F_MMAPABLE is supported for arrays */
	FEAT_ARRAY_MMAP,
	/* kernel support for expected_attach_type in BPF_PROG_LOAD */
	FEAT_EXP_ATTACH_TYPE,
	/* bpf_probe_read_{kernel,user}[_str] helpers */
	FEAT_PROBE_READ_KERN,
	/* BPF_PROG_BIND_MAP is supported */
	FEAT_PROG_BIND_MAP,
	/* Kernel support for module BTFs */
	FEAT_MODULE_BTF,
	/* BTF_KIND_FLOAT support */
	FEAT_BTF_FLOAT,
	/* BPF perf link support */
	FEAT_PERF_LINK,
	__FEAT_CNT,
};

static bool kernel_supports(const struct bpf_object *obj, enum kern_feature_id feat_id);

enum reloc_type {
	RELO_LD64,
	RELO_CALL,
	RELO_DATA,
	RELO_EXTERN_VAR,
	RELO_EXTERN_FUNC,
	RELO_SUBPROG_ADDR,
};

struct reloc_desc {
	enum reloc_type type;
	int insn_idx;
	int map_idx;
	int sym_off;
};

struct bpf_sec_def;

typedef struct bpf_link *(*attach_fn_t)(const struct bpf_sec_def *sec,
					struct bpf_program *prog);

struct bpf_sec_def {
	const char *sec;
	size_t len;
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	bool is_exp_attach_type_optional;
	bool is_attachable;
	bool is_attach_btf;
	bool is_sleepable;
	attach_fn_t attach_fn;
};

/*
 * bpf_prog should be a better name but it has been used in
 * linux/filter.h.
 */
struct bpf_program {
	const struct bpf_sec_def *sec_def;
	char *sec_name;
	size_t sec_idx;
	/* this program's instruction offset (in number of instructions)
	 * within its containing ELF section
	 */
	size_t sec_insn_off;
	/* number of original instructions in ELF section belonging to this
	 * program, not taking into account subprogram instructions possible
	 * appended later during relocation
	 */
	size_t sec_insn_cnt;
	/* Offset (in number of instructions) of the start of instruction
	 * belonging to this BPF program  within its containing main BPF
	 * program. For the entry-point (main) BPF program, this is always
	 * zero. For a sub-program, this gets reset before each of main BPF
	 * programs are processed and relocated and is used to determined
	 * whether sub-program was already appended to the main program, and
	 * if yes, at which instruction offset.
	 */
	size_t sub_insn_off;

	char *name;
	/* sec_name with / replaced by _; makes recursive pinning
	 * in bpf_object__pin_programs easier
	 */
	char *pin_name;

	/* instructions that belong to BPF program; insns[0] is located at
	 * sec_insn_off instruction within its ELF section in ELF file, so
	 * when mapping ELF file instruction index to the local instruction,
	 * one needs to subtract sec_insn_off; and vice versa.
	 */
	struct bpf_insn *insns;
	/* actual number of instruction in this BPF program's image; for
	 * entry-point BPF programs this includes the size of main program
	 * itself plus all the used sub-programs, appended at the end
	 */
	size_t insns_cnt;

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

	bool load;
	bool mark_btf_static;
	enum bpf_prog_type type;
	enum bpf_attach_type expected_attach_type;
	int prog_ifindex;
	__u32 attach_btf_obj_fd;
	__u32 attach_btf_id;
	__u32 attach_prog_fd;
	void *func_info;
	__u32 func_info_rec_size;
	__u32 func_info_cnt;

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
#define KSYMS_SEC ".ksyms"
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
	__u32 numa_node;
	__u32 btf_var_idx;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
	__u32 btf_vmlinux_value_type_id;
	void *priv;
	bpf_map_clear_priv_t clear_priv;
	enum libbpf_map_type libbpf_type;
	void *mmaped;
	struct bpf_struct_ops *st_ops;
	struct bpf_map *inner_map;
	void **init_slots;
	int init_slots_sz;
	char *pin_path;
	bool pinned;
	bool reused;
};

enum extern_type {
	EXT_UNKNOWN,
	EXT_KCFG,
	EXT_KSYM,
};

enum kcfg_type {
	KCFG_UNKNOWN,
	KCFG_CHAR,
	KCFG_BOOL,
	KCFG_INT,
	KCFG_TRISTATE,
	KCFG_CHAR_ARR,
};

struct extern_desc {
	enum extern_type type;
	int sym_idx;
	int btf_id;
	int sec_btf_id;
	const char *name;
	bool is_set;
	bool is_weak;
	union {
		struct {
			enum kcfg_type type;
			int sz;
			int align;
			int data_off;
			bool is_signed;
		} kcfg;
		struct {
			unsigned long long addr;

			/* target btf_id of the corresponding kernel var. */
			int kernel_btf_obj_fd;
			int kernel_btf_id;

			/* local btf_id of the ksym extern's type. */
			__u32 type_id;
		} ksym;
	};
};

static LIST_HEAD(bpf_objects_list);

struct module_btf {
	struct btf *btf;
	char *name;
	__u32 id;
	int fd;
};

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
	int rodata_map_idx;

	bool loaded;
	bool has_subcalls;

	struct bpf_gen *gen_loader;

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
		size_t shstrndx; /* section index for section name strings */
		size_t strtabidx;
		struct {
			GElf_Shdr shdr;
			Elf_Data *data;
		} *reloc_sects;
		int nr_reloc_sects;
		int maps_shndx;
		int btf_maps_shndx;
		__u32 btf_maps_sec_btf_id;
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
	struct btf_ext *btf_ext;

	/* Parse and load BTF vmlinux if any of the programs in the object need
	 * it at load time.
	 */
	struct btf *btf_vmlinux;
	/* Path to the custom BTF to be used for BPF CO-RE relocations as an
	 * override for vmlinux BTF.
	 */
	char *btf_custom_path;
	/* vmlinux BTF override for CO-RE relocations */
	struct btf *btf_vmlinux_override;
	/* Lazily initialized kernel module BTFs */
	struct module_btf *btf_modules;
	bool btf_modules_loaded;
	size_t btf_module_cnt;
	size_t btf_module_cap;

	void *priv;
	bpf_object_clear_priv_t clear_priv;

	char path[];
};
#define obj_elf_valid(o)	((o)->efile.elf)

static const char *elf_sym_str(const struct bpf_object *obj, size_t off);
static const char *elf_sec_str(const struct bpf_object *obj, size_t off);
static Elf_Scn *elf_sec_by_idx(const struct bpf_object *obj, size_t idx);
static Elf_Scn *elf_sec_by_name(const struct bpf_object *obj, const char *name);
static int elf_sec_hdr(const struct bpf_object *obj, Elf_Scn *scn, GElf_Shdr *hdr);
static const char *elf_sec_name(const struct bpf_object *obj, Elf_Scn *scn);
static Elf_Data *elf_sec_data(const struct bpf_object *obj, Elf_Scn *scn);

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
	zfree(&prog->sec_name);
	zfree(&prog->pin_name);
	zfree(&prog->insns);
	zfree(&prog->reloc_desc);

	prog->nr_reloc = 0;
	prog->insns_cnt = 0;
	prog->sec_idx = -1;
}

static char *__bpf_program__pin_name(struct bpf_program *prog)
{
	char *name, *p;

	name = p = strdup(prog->sec_name);
	while ((p = strchr(p, '/')))
		*p = '_';

	return name;
}

static bool insn_is_subprog_call(const struct bpf_insn *insn)
{
	return BPF_CLASS(insn->code) == BPF_JMP &&
	       BPF_OP(insn->code) == BPF_CALL &&
	       BPF_SRC(insn->code) == BPF_K &&
	       insn->src_reg == BPF_PSEUDO_CALL &&
	       insn->dst_reg == 0 &&
	       insn->off == 0;
}

static bool is_call_insn(const struct bpf_insn *insn)
{
	return insn->code == (BPF_JMP | BPF_CALL);
}

static bool insn_is_pseudo_func(struct bpf_insn *insn)
{
	return is_ldimm64_insn(insn) && insn->src_reg == BPF_PSEUDO_FUNC;
}

static int
bpf_object__init_prog(struct bpf_object *obj, struct bpf_program *prog,
		      const char *name, size_t sec_idx, const char *sec_name,
		      size_t sec_off, void *insn_data, size_t insn_data_sz)
{
	if (insn_data_sz == 0 || insn_data_sz % BPF_INSN_SZ || sec_off % BPF_INSN_SZ) {
		pr_warn("sec '%s': corrupted program '%s', offset %zu, size %zu\n",
			sec_name, name, sec_off, insn_data_sz);
		return -EINVAL;
	}

	memset(prog, 0, sizeof(*prog));
	prog->obj = obj;

	prog->sec_idx = sec_idx;
	prog->sec_insn_off = sec_off / BPF_INSN_SZ;
	prog->sec_insn_cnt = insn_data_sz / BPF_INSN_SZ;
	/* insns_cnt can later be increased by appending used subprograms */
	prog->insns_cnt = prog->sec_insn_cnt;

	prog->type = BPF_PROG_TYPE_UNSPEC;
	prog->load = true;

	prog->instances.fds = NULL;
	prog->instances.nr = -1;

	prog->sec_name = strdup(sec_name);
	if (!prog->sec_name)
		goto errout;

	prog->name = strdup(name);
	if (!prog->name)
		goto errout;

	prog->pin_name = __bpf_program__pin_name(prog);
	if (!prog->pin_name)
		goto errout;

	prog->insns = malloc(insn_data_sz);
	if (!prog->insns)
		goto errout;
	memcpy(prog->insns, insn_data, insn_data_sz);

	return 0;
errout:
	pr_warn("sec '%s': failed to allocate memory for prog '%s'\n", sec_name, name);
	bpf_program__exit(prog);
	return -ENOMEM;
}

static int
bpf_object__add_programs(struct bpf_object *obj, Elf_Data *sec_data,
			 const char *sec_name, int sec_idx)
{
	Elf_Data *symbols = obj->efile.symbols;
	struct bpf_program *prog, *progs;
	void *data = sec_data->d_buf;
	size_t sec_sz = sec_data->d_size, sec_off, prog_sz, nr_syms;
	int nr_progs, err, i;
	const char *name;
	GElf_Sym sym;

	progs = obj->programs;
	nr_progs = obj->nr_programs;
	nr_syms = symbols->d_size / sizeof(GElf_Sym);
	sec_off = 0;

	for (i = 0; i < nr_syms; i++) {
		if (!gelf_getsym(symbols, i, &sym))
			continue;
		if (sym.st_shndx != sec_idx)
			continue;
		if (GELF_ST_TYPE(sym.st_info) != STT_FUNC)
			continue;

		prog_sz = sym.st_size;
		sec_off = sym.st_value;

		name = elf_sym_str(obj, sym.st_name);
		if (!name) {
			pr_warn("sec '%s': failed to get symbol name for offset %zu\n",
				sec_name, sec_off);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (sec_off + prog_sz > sec_sz) {
			pr_warn("sec '%s': program at offset %zu crosses section boundary\n",
				sec_name, sec_off);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (sec_idx != obj->efile.text_shndx && GELF_ST_BIND(sym.st_info) == STB_LOCAL) {
			pr_warn("sec '%s': program '%s' is static and not supported\n", sec_name, name);
			return -ENOTSUP;
		}

		pr_debug("sec '%s': found program '%s' at insn offset %zu (%zu bytes), code size %zu insns (%zu bytes)\n",
			 sec_name, name, sec_off / BPF_INSN_SZ, sec_off, prog_sz / BPF_INSN_SZ, prog_sz);

		progs = libbpf_reallocarray(progs, nr_progs + 1, sizeof(*progs));
		if (!progs) {
			/*
			 * In this case the original obj->programs
			 * is still valid, so don't need special treat for
			 * bpf_close_object().
			 */
			pr_warn("sec '%s': failed to alloc memory for new program '%s'\n",
				sec_name, name);
			return -ENOMEM;
		}
		obj->programs = progs;

		prog = &progs[nr_progs];

		err = bpf_object__init_prog(obj, prog, name, sec_idx, sec_name,
					    sec_off, data + sec_off, prog_sz);
		if (err)
			return err;

		/* if function is a global/weak symbol, but has restricted
		 * (STV_HIDDEN or STV_INTERNAL) visibility, mark its BTF FUNC
		 * as static to enable more permissive BPF verification mode
		 * with more outside context available to BPF verifier
		 */
		if (GELF_ST_BIND(sym.st_info) != STB_LOCAL
		    && (GELF_ST_VISIBILITY(sym.st_other) == STV_HIDDEN
			|| GELF_ST_VISIBILITY(sym.st_other) == STV_INTERNAL))
			prog->mark_btf_static = true;

		nr_progs++;
		obj->nr_programs = nr_progs;
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

			prog = st_ops->progs[i];
			if (!prog)
				continue;

			kern_mtype = skip_mods_and_typedefs(kern_btf,
							    kern_mtype->type,
							    &kern_mtype_id);

			/* mtype->type must be a func_proto which was
			 * guaranteed in bpf_object__collect_st_ops_relos(),
			 * so only check kern_mtype for func_proto here.
			 */
			if (!btf_is_func_proto(kern_mtype)) {
				pr_warn("struct_ops init_kern %s: kernel member %s is not a func ptr\n",
					map->name, mname);
				return -ENOTSUP;
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
	obj->rodata_map_idx = -1;

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
		pr_warn("elf: init internal error\n");
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
			pr_warn("elf: failed to open %s: %s\n", obj->path, cp);
			return err;
		}

		obj->efile.elf = elf_begin(obj->efile.fd, ELF_C_READ_MMAP, NULL);
	}

	if (!obj->efile.elf) {
		pr_warn("elf: failed to open %s as ELF file: %s\n", obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__LIBELF;
		goto errout;
	}

	if (!gelf_getehdr(obj->efile.elf, &obj->efile.ehdr)) {
		pr_warn("elf: failed to get ELF header from %s: %s\n", obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}
	ep = &obj->efile.ehdr;

	if (elf_getshdrstrndx(obj->efile.elf, &obj->efile.shstrndx)) {
		pr_warn("elf: failed to get section names section index for %s: %s\n",
			obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}

	/* Elf is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(obj->efile.elf, obj->efile.shstrndx), NULL)) {
		pr_warn("elf: failed to get section names strings from %s: %s\n",
			obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}

	/* Old LLVM set e_machine to EM_NONE */
	if (ep->e_type != ET_REL ||
	    (ep->e_machine && ep->e_machine != EM_BPF)) {
		pr_warn("elf: %s is not a valid eBPF object file\n", obj->path);
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
	pr_warn("elf: endianness mismatch in %s.\n", obj->path);
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

int bpf_object__section_size(const struct bpf_object *obj, const char *name,
			     __u32 *size)
{
	int ret = -ENOENT;

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
		Elf_Scn *scn = elf_sec_by_name(obj, name);
		Elf_Data *data = elf_sec_data(obj, scn);

		if (data) {
			ret = 0; /* found it */
			*size = data->d_size;
		}
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

		sname = elf_sym_str(obj, sym.st_name);
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
	new_maps = libbpf_reallocarray(obj->maps, new_cap, sizeof(*obj->maps));
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

		obj->rodata_map_idx = obj->nr_maps - 1;
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

static int set_kcfg_value_tri(struct extern_desc *ext, void *ext_val,
			      char value)
{
	switch (ext->kcfg.type) {
	case KCFG_BOOL:
		if (value == 'm') {
			pr_warn("extern (kcfg) %s=%c should be tristate or char\n",
				ext->name, value);
			return -EINVAL;
		}
		*(bool *)ext_val = value == 'y' ? true : false;
		break;
	case KCFG_TRISTATE:
		if (value == 'y')
			*(enum libbpf_tristate *)ext_val = TRI_YES;
		else if (value == 'm')
			*(enum libbpf_tristate *)ext_val = TRI_MODULE;
		else /* value == 'n' */
			*(enum libbpf_tristate *)ext_val = TRI_NO;
		break;
	case KCFG_CHAR:
		*(char *)ext_val = value;
		break;
	case KCFG_UNKNOWN:
	case KCFG_INT:
	case KCFG_CHAR_ARR:
	default:
		pr_warn("extern (kcfg) %s=%c should be bool, tristate, or char\n",
			ext->name, value);
		return -EINVAL;
	}
	ext->is_set = true;
	return 0;
}

static int set_kcfg_value_str(struct extern_desc *ext, char *ext_val,
			      const char *value)
{
	size_t len;

	if (ext->kcfg.type != KCFG_CHAR_ARR) {
		pr_warn("extern (kcfg) %s=%s should be char array\n", ext->name, value);
		return -EINVAL;
	}

	len = strlen(value);
	if (value[len - 1] != '"') {
		pr_warn("extern (kcfg) '%s': invalid string config '%s'\n",
			ext->name, value);
		return -EINVAL;
	}

	/* strip quotes */
	len -= 2;
	if (len >= ext->kcfg.sz) {
		pr_warn("extern (kcfg) '%s': long string config %s of (%zu bytes) truncated to %d bytes\n",
			ext->name, value, len, ext->kcfg.sz - 1);
		len = ext->kcfg.sz - 1;
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

static bool is_kcfg_value_in_range(const struct extern_desc *ext, __u64 v)
{
	int bit_sz = ext->kcfg.sz * 8;

	if (ext->kcfg.sz == 8)
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
	if (ext->kcfg.is_signed)
		return v + (1ULL << (bit_sz - 1)) < (1ULL << bit_sz);
	else
		return (v >> bit_sz) == 0;
}

static int set_kcfg_value_num(struct extern_desc *ext, void *ext_val,
			      __u64 value)
{
	if (ext->kcfg.type != KCFG_INT && ext->kcfg.type != KCFG_CHAR) {
		pr_warn("extern (kcfg) %s=%llu should be integer\n",
			ext->name, (unsigned long long)value);
		return -EINVAL;
	}
	if (!is_kcfg_value_in_range(ext, value)) {
		pr_warn("extern (kcfg) %s=%llu value doesn't fit in %d bytes\n",
			ext->name, (unsigned long long)value, ext->kcfg.sz);
		return -ERANGE;
	}
	switch (ext->kcfg.sz) {
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

	ext_val = data + ext->kcfg.data_off;
	value = sep + 1;

	switch (*value) {
	case 'y': case 'n': case 'm':
		err = set_kcfg_value_tri(ext, ext_val, *value);
		break;
	case '"':
		err = set_kcfg_value_str(ext, ext_val, value);
		break;
	default:
		/* assume integer */
		err = parse_u64(value, &num);
		if (err) {
			pr_warn("extern (kcfg) %s=%s should be integer\n",
				ext->name, value);
			return err;
		}
		err = set_kcfg_value_num(ext, ext_val, num);
		break;
	}
	if (err)
		return err;
	pr_debug("extern (kcfg) %s=%s\n", ext->name, value);
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
	struct extern_desc *last_ext = NULL, *ext;
	size_t map_sz;
	int i, err;

	for (i = 0; i < obj->nr_extern; i++) {
		ext = &obj->externs[i];
		if (ext->type == EXT_KCFG)
			last_ext = ext;
	}

	if (!last_ext)
		return 0;

	map_sz = last_ext->kcfg.data_off + last_ext->kcfg.sz;
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

	scn = elf_sec_by_idx(obj, obj->efile.maps_shndx);
	data = elf_sec_data(obj, scn);
	if (!scn || !data) {
		pr_warn("elf: failed to get legacy map definitions for %s\n",
			obj->path);
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
	pr_debug("elf: found %d legacy map definitions (%zd bytes) in %s\n",
		 nr_maps, data->d_size, obj->path);

	if (!data->d_size || nr_maps == 0 || (data->d_size % nr_maps) != 0) {
		pr_warn("elf: unable to determine legacy map definition size in %s\n",
			obj->path);
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

		map_name = elf_sym_str(obj, sym.st_name);
		if (!map_name) {
			pr_warn("failed to get map #%d name sym string for obj %s\n",
				i, obj->path);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (GELF_ST_TYPE(sym.st_info) == STT_SECTION
		    || GELF_ST_BIND(sym.st_info) == STB_LOCAL) {
			pr_warn("map '%s' (legacy): static maps are not supported\n", map_name);
			return -ENOTSUP;
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

const struct btf_type *
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

static const char *__btf_kind_str(__u16 kind)
{
	switch (kind) {
	case BTF_KIND_UNKN: return "void";
	case BTF_KIND_INT: return "int";
	case BTF_KIND_PTR: return "ptr";
	case BTF_KIND_ARRAY: return "array";
	case BTF_KIND_STRUCT: return "struct";
	case BTF_KIND_UNION: return "union";
	case BTF_KIND_ENUM: return "enum";
	case BTF_KIND_FWD: return "fwd";
	case BTF_KIND_TYPEDEF: return "typedef";
	case BTF_KIND_VOLATILE: return "volatile";
	case BTF_KIND_CONST: return "const";
	case BTF_KIND_RESTRICT: return "restrict";
	case BTF_KIND_FUNC: return "func";
	case BTF_KIND_FUNC_PROTO: return "func_proto";
	case BTF_KIND_VAR: return "var";
	case BTF_KIND_DATASEC: return "datasec";
	case BTF_KIND_FLOAT: return "float";
	default: return "unknown";
	}
}

const char *btf_kind_str(const struct btf_type *t)
{
	return __btf_kind_str(btf_kind(t));
}

/*
 * Fetch integer attribute of BTF map definition. Such attributes are
 * represented using a pointer to an array, in which dimensionality of array
 * encodes specified integer value. E.g., int (*type)[BPF_MAP_TYPE_ARRAY];
 * encodes `type => BPF_MAP_TYPE_ARRAY` key/value pair completely using BTF
 * type definition, while using only sizeof(void *) space in ELF data section.
 */
static bool get_map_field_int(const char *map_name, const struct btf *btf,
			      const struct btf_member *m, __u32 *res)
{
	const struct btf_type *t = skip_mods_and_typedefs(btf, m->type, NULL);
	const char *name = btf__name_by_offset(btf, m->name_off);
	const struct btf_array *arr_info;
	const struct btf_type *arr_t;

	if (!btf_is_ptr(t)) {
		pr_warn("map '%s': attr '%s': expected PTR, got %s.\n",
			map_name, name, btf_kind_str(t));
		return false;
	}

	arr_t = btf__type_by_id(btf, t->type);
	if (!arr_t) {
		pr_warn("map '%s': attr '%s': type [%u] not found.\n",
			map_name, name, t->type);
		return false;
	}
	if (!btf_is_array(arr_t)) {
		pr_warn("map '%s': attr '%s': expected ARRAY, got %s.\n",
			map_name, name, btf_kind_str(arr_t));
		return false;
	}
	arr_info = btf_array(arr_t);
	*res = arr_info->nelems;
	return true;
}

static int build_map_pin_path(struct bpf_map *map, const char *path)
{
	char buf[PATH_MAX];
	int len;

	if (!path)
		path = "/sys/fs/bpf";

	len = snprintf(buf, PATH_MAX, "%s/%s", path, bpf_map__name(map));
	if (len < 0)
		return -EINVAL;
	else if (len >= PATH_MAX)
		return -ENAMETOOLONG;

	return bpf_map__set_pin_path(map, buf);
}

int parse_btf_map_def(const char *map_name, struct btf *btf,
		      const struct btf_type *def_t, bool strict,
		      struct btf_map_def *map_def, struct btf_map_def *inner_def)
{
	const struct btf_type *t;
	const struct btf_member *m;
	bool is_inner = inner_def == NULL;
	int vlen, i;

	vlen = btf_vlen(def_t);
	m = btf_members(def_t);
	for (i = 0; i < vlen; i++, m++) {
		const char *name = btf__name_by_offset(btf, m->name_off);

		if (!name) {
			pr_warn("map '%s': invalid field #%d.\n", map_name, i);
			return -EINVAL;
		}
		if (strcmp(name, "type") == 0) {
			if (!get_map_field_int(map_name, btf, m, &map_def->map_type))
				return -EINVAL;
			map_def->parts |= MAP_DEF_MAP_TYPE;
		} else if (strcmp(name, "max_entries") == 0) {
			if (!get_map_field_int(map_name, btf, m, &map_def->max_entries))
				return -EINVAL;
			map_def->parts |= MAP_DEF_MAX_ENTRIES;
		} else if (strcmp(name, "map_flags") == 0) {
			if (!get_map_field_int(map_name, btf, m, &map_def->map_flags))
				return -EINVAL;
			map_def->parts |= MAP_DEF_MAP_FLAGS;
		} else if (strcmp(name, "numa_node") == 0) {
			if (!get_map_field_int(map_name, btf, m, &map_def->numa_node))
				return -EINVAL;
			map_def->parts |= MAP_DEF_NUMA_NODE;
		} else if (strcmp(name, "key_size") == 0) {
			__u32 sz;

			if (!get_map_field_int(map_name, btf, m, &sz))
				return -EINVAL;
			if (map_def->key_size && map_def->key_size != sz) {
				pr_warn("map '%s': conflicting key size %u != %u.\n",
					map_name, map_def->key_size, sz);
				return -EINVAL;
			}
			map_def->key_size = sz;
			map_def->parts |= MAP_DEF_KEY_SIZE;
		} else if (strcmp(name, "key") == 0) {
			__s64 sz;

			t = btf__type_by_id(btf, m->type);
			if (!t) {
				pr_warn("map '%s': key type [%d] not found.\n",
					map_name, m->type);
				return -EINVAL;
			}
			if (!btf_is_ptr(t)) {
				pr_warn("map '%s': key spec is not PTR: %s.\n",
					map_name, btf_kind_str(t));
				return -EINVAL;
			}
			sz = btf__resolve_size(btf, t->type);
			if (sz < 0) {
				pr_warn("map '%s': can't determine key size for type [%u]: %zd.\n",
					map_name, t->type, (ssize_t)sz);
				return sz;
			}
			if (map_def->key_size && map_def->key_size != sz) {
				pr_warn("map '%s': conflicting key size %u != %zd.\n",
					map_name, map_def->key_size, (ssize_t)sz);
				return -EINVAL;
			}
			map_def->key_size = sz;
			map_def->key_type_id = t->type;
			map_def->parts |= MAP_DEF_KEY_SIZE | MAP_DEF_KEY_TYPE;
		} else if (strcmp(name, "value_size") == 0) {
			__u32 sz;

			if (!get_map_field_int(map_name, btf, m, &sz))
				return -EINVAL;
			if (map_def->value_size && map_def->value_size != sz) {
				pr_warn("map '%s': conflicting value size %u != %u.\n",
					map_name, map_def->value_size, sz);
				return -EINVAL;
			}
			map_def->value_size = sz;
			map_def->parts |= MAP_DEF_VALUE_SIZE;
		} else if (strcmp(name, "value") == 0) {
			__s64 sz;

			t = btf__type_by_id(btf, m->type);
			if (!t) {
				pr_warn("map '%s': value type [%d] not found.\n",
					map_name, m->type);
				return -EINVAL;
			}
			if (!btf_is_ptr(t)) {
				pr_warn("map '%s': value spec is not PTR: %s.\n",
					map_name, btf_kind_str(t));
				return -EINVAL;
			}
			sz = btf__resolve_size(btf, t->type);
			if (sz < 0) {
				pr_warn("map '%s': can't determine value size for type [%u]: %zd.\n",
					map_name, t->type, (ssize_t)sz);
				return sz;
			}
			if (map_def->value_size && map_def->value_size != sz) {
				pr_warn("map '%s': conflicting value size %u != %zd.\n",
					map_name, map_def->value_size, (ssize_t)sz);
				return -EINVAL;
			}
			map_def->value_size = sz;
			map_def->value_type_id = t->type;
			map_def->parts |= MAP_DEF_VALUE_SIZE | MAP_DEF_VALUE_TYPE;
		}
		else if (strcmp(name, "values") == 0) {
			char inner_map_name[128];
			int err;

			if (is_inner) {
				pr_warn("map '%s': multi-level inner maps not supported.\n",
					map_name);
				return -ENOTSUP;
			}
			if (i != vlen - 1) {
				pr_warn("map '%s': '%s' member should be last.\n",
					map_name, name);
				return -EINVAL;
			}
			if (!bpf_map_type__is_map_in_map(map_def->map_type)) {
				pr_warn("map '%s': should be map-in-map.\n",
					map_name);
				return -ENOTSUP;
			}
			if (map_def->value_size && map_def->value_size != 4) {
				pr_warn("map '%s': conflicting value size %u != 4.\n",
					map_name, map_def->value_size);
				return -EINVAL;
			}
			map_def->value_size = 4;
			t = btf__type_by_id(btf, m->type);
			if (!t) {
				pr_warn("map '%s': map-in-map inner type [%d] not found.\n",
					map_name, m->type);
				return -EINVAL;
			}
			if (!btf_is_array(t) || btf_array(t)->nelems) {
				pr_warn("map '%s': map-in-map inner spec is not a zero-sized array.\n",
					map_name);
				return -EINVAL;
			}
			t = skip_mods_and_typedefs(btf, btf_array(t)->type, NULL);
			if (!btf_is_ptr(t)) {
				pr_warn("map '%s': map-in-map inner def is of unexpected kind %s.\n",
					map_name, btf_kind_str(t));
				return -EINVAL;
			}
			t = skip_mods_and_typedefs(btf, t->type, NULL);
			if (!btf_is_struct(t)) {
				pr_warn("map '%s': map-in-map inner def is of unexpected kind %s.\n",
					map_name, btf_kind_str(t));
				return -EINVAL;
			}

			snprintf(inner_map_name, sizeof(inner_map_name), "%s.inner", map_name);
			err = parse_btf_map_def(inner_map_name, btf, t, strict, inner_def, NULL);
			if (err)
				return err;

			map_def->parts |= MAP_DEF_INNER_MAP;
		} else if (strcmp(name, "pinning") == 0) {
			__u32 val;

			if (is_inner) {
				pr_warn("map '%s': inner def can't be pinned.\n", map_name);
				return -EINVAL;
			}
			if (!get_map_field_int(map_name, btf, m, &val))
				return -EINVAL;
			if (val != LIBBPF_PIN_NONE && val != LIBBPF_PIN_BY_NAME) {
				pr_warn("map '%s': invalid pinning value %u.\n",
					map_name, val);
				return -EINVAL;
			}
			map_def->pinning = val;
			map_def->parts |= MAP_DEF_PINNING;
		} else {
			if (strict) {
				pr_warn("map '%s': unknown field '%s'.\n", map_name, name);
				return -ENOTSUP;
			}
			pr_debug("map '%s': ignoring unknown field '%s'.\n", map_name, name);
		}
	}

	if (map_def->map_type == BPF_MAP_TYPE_UNSPEC) {
		pr_warn("map '%s': map type isn't specified.\n", map_name);
		return -EINVAL;
	}

	return 0;
}

static void fill_map_from_def(struct bpf_map *map, const struct btf_map_def *def)
{
	map->def.type = def->map_type;
	map->def.key_size = def->key_size;
	map->def.value_size = def->value_size;
	map->def.max_entries = def->max_entries;
	map->def.map_flags = def->map_flags;

	map->numa_node = def->numa_node;
	map->btf_key_type_id = def->key_type_id;
	map->btf_value_type_id = def->value_type_id;

	if (def->parts & MAP_DEF_MAP_TYPE)
		pr_debug("map '%s': found type = %u.\n", map->name, def->map_type);

	if (def->parts & MAP_DEF_KEY_TYPE)
		pr_debug("map '%s': found key [%u], sz = %u.\n",
			 map->name, def->key_type_id, def->key_size);
	else if (def->parts & MAP_DEF_KEY_SIZE)
		pr_debug("map '%s': found key_size = %u.\n", map->name, def->key_size);

	if (def->parts & MAP_DEF_VALUE_TYPE)
		pr_debug("map '%s': found value [%u], sz = %u.\n",
			 map->name, def->value_type_id, def->value_size);
	else if (def->parts & MAP_DEF_VALUE_SIZE)
		pr_debug("map '%s': found value_size = %u.\n", map->name, def->value_size);

	if (def->parts & MAP_DEF_MAX_ENTRIES)
		pr_debug("map '%s': found max_entries = %u.\n", map->name, def->max_entries);
	if (def->parts & MAP_DEF_MAP_FLAGS)
		pr_debug("map '%s': found map_flags = %u.\n", map->name, def->map_flags);
	if (def->parts & MAP_DEF_PINNING)
		pr_debug("map '%s': found pinning = %u.\n", map->name, def->pinning);
	if (def->parts & MAP_DEF_NUMA_NODE)
		pr_debug("map '%s': found numa_node = %u.\n", map->name, def->numa_node);

	if (def->parts & MAP_DEF_INNER_MAP)
		pr_debug("map '%s': found inner map definition.\n", map->name);
}

static const char *btf_var_linkage_str(__u32 linkage)
{
	switch (linkage) {
	case BTF_VAR_STATIC: return "static";
	case BTF_VAR_GLOBAL_ALLOCATED: return "global";
	case BTF_VAR_GLOBAL_EXTERN: return "extern";
	default: return "unknown";
	}
}

static int bpf_object__init_user_btf_map(struct bpf_object *obj,
					 const struct btf_type *sec,
					 int var_idx, int sec_idx,
					 const Elf_Data *data, bool strict,
					 const char *pin_root_path)
{
	struct btf_map_def map_def = {}, inner_def = {};
	const struct btf_type *var, *def;
	const struct btf_var_secinfo *vi;
	const struct btf_var *var_extra;
	const char *map_name;
	struct bpf_map *map;
	int err;

	vi = btf_var_secinfos(sec) + var_idx;
	var = btf__type_by_id(obj->btf, vi->type);
	var_extra = btf_var(var);
	map_name = btf__name_by_offset(obj->btf, var->name_off);

	if (map_name == NULL || map_name[0] == '\0') {
		pr_warn("map #%d: empty name.\n", var_idx);
		return -EINVAL;
	}
	if ((__u64)vi->offset + vi->size > data->d_size) {
		pr_warn("map '%s' BTF data is corrupted.\n", map_name);
		return -EINVAL;
	}
	if (!btf_is_var(var)) {
		pr_warn("map '%s': unexpected var kind %s.\n",
			map_name, btf_kind_str(var));
		return -EINVAL;
	}
	if (var_extra->linkage != BTF_VAR_GLOBAL_ALLOCATED) {
		pr_warn("map '%s': unsupported map linkage %s.\n",
			map_name, btf_var_linkage_str(var_extra->linkage));
		return -EOPNOTSUPP;
	}

	def = skip_mods_and_typedefs(obj->btf, var->type, NULL);
	if (!btf_is_struct(def)) {
		pr_warn("map '%s': unexpected def kind %s.\n",
			map_name, btf_kind_str(var));
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
	map->btf_var_idx = var_idx;
	pr_debug("map '%s': at sec_idx %d, offset %zu.\n",
		 map_name, map->sec_idx, map->sec_offset);

	err = parse_btf_map_def(map->name, obj->btf, def, strict, &map_def, &inner_def);
	if (err)
		return err;

	fill_map_from_def(map, &map_def);

	if (map_def.pinning == LIBBPF_PIN_BY_NAME) {
		err = build_map_pin_path(map, pin_root_path);
		if (err) {
			pr_warn("map '%s': couldn't build pin path.\n", map->name);
			return err;
		}
	}

	if (map_def.parts & MAP_DEF_INNER_MAP) {
		map->inner_map = calloc(1, sizeof(*map->inner_map));
		if (!map->inner_map)
			return -ENOMEM;
		map->inner_map->fd = -1;
		map->inner_map->sec_idx = sec_idx;
		map->inner_map->name = malloc(strlen(map_name) + sizeof(".inner") + 1);
		if (!map->inner_map->name)
			return -ENOMEM;
		sprintf(map->inner_map->name, "%s.inner", map_name);

		fill_map_from_def(map->inner_map, &inner_def);
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

	scn = elf_sec_by_idx(obj, obj->efile.btf_maps_shndx);
	data = elf_sec_data(obj, scn);
	if (!scn || !data) {
		pr_warn("elf: failed to get %s map definitions for %s\n",
			MAPS_ELF_SEC, obj->path);
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
			obj->efile.btf_maps_sec_btf_id = i;
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

	return err;
}

static bool section_have_execinstr(struct bpf_object *obj, int idx)
{
	GElf_Shdr sh;

	if (elf_sec_hdr(obj, elf_sec_by_idx(obj, idx), &sh))
		return false;

	return sh.sh_flags & SHF_EXECINSTR;
}

static bool btf_needs_sanitization(struct bpf_object *obj)
{
	bool has_func_global = kernel_supports(obj, FEAT_BTF_GLOBAL_FUNC);
	bool has_datasec = kernel_supports(obj, FEAT_BTF_DATASEC);
	bool has_float = kernel_supports(obj, FEAT_BTF_FLOAT);
	bool has_func = kernel_supports(obj, FEAT_BTF_FUNC);

	return !has_func || !has_datasec || !has_func_global || !has_float;
}

static void bpf_object__sanitize_btf(struct bpf_object *obj, struct btf *btf)
{
	bool has_func_global = kernel_supports(obj, FEAT_BTF_GLOBAL_FUNC);
	bool has_datasec = kernel_supports(obj, FEAT_BTF_DATASEC);
	bool has_float = kernel_supports(obj, FEAT_BTF_FLOAT);
	bool has_func = kernel_supports(obj, FEAT_BTF_FUNC);
	struct btf_type *t;
	int i, j, vlen;

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
		} else if (!has_float && btf_is_float(t)) {
			/* replace FLOAT with an equally-sized empty STRUCT;
			 * since C compilers do not accept e.g. "float" as a
			 * valid struct name, make it anonymous
			 */
			t->name_off = 0;
			t->info = BTF_INFO_ENC(BTF_KIND_STRUCT, 0, 0);
		}
	}
}

static bool libbpf_needs_btf(const struct bpf_object *obj)
{
	return obj->efile.btf_maps_shndx >= 0 ||
	       obj->efile.st_ops_shndx >= 0 ||
	       obj->nr_extern > 0;
}

static bool kernel_needs_btf(const struct bpf_object *obj)
{
	return obj->efile.st_ops_shndx >= 0;
}

static int bpf_object__init_btf(struct bpf_object *obj,
				Elf_Data *btf_data,
				Elf_Data *btf_ext_data)
{
	int err = -ENOENT;

	if (btf_data) {
		obj->btf = btf__new(btf_data->d_buf, btf_data->d_size);
		err = libbpf_get_error(obj->btf);
		if (err) {
			obj->btf = NULL;
			pr_warn("Error loading ELF section %s: %d.\n", BTF_ELF_SEC, err);
			goto out;
		}
		/* enforce 8-byte pointers for BPF-targeted BTFs */
		btf__set_pointer_size(obj->btf, 8);
	}
	if (btf_ext_data) {
		if (!obj->btf) {
			pr_debug("Ignore ELF section %s because its depending ELF section %s is not found.\n",
				 BTF_EXT_ELF_SEC, BTF_ELF_SEC);
			goto out;
		}
		obj->btf_ext = btf_ext__new(btf_ext_data->d_buf, btf_ext_data->d_size);
		err = libbpf_get_error(obj->btf_ext);
		if (err) {
			pr_warn("Error loading ELF section %s: %d. Ignored and continue.\n",
				BTF_EXT_ELF_SEC, err);
			obj->btf_ext = NULL;
			goto out;
		}
	}
out:
	if (err && libbpf_needs_btf(obj)) {
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
	if (err) {
		pr_warn("Error finalizing %s: %d.\n", BTF_ELF_SEC, err);
		return err;
	}

	return 0;
}

static bool prog_needs_vmlinux_btf(struct bpf_program *prog)
{
	if (prog->type == BPF_PROG_TYPE_STRUCT_OPS ||
	    prog->type == BPF_PROG_TYPE_LSM)
		return true;

	/* BPF_PROG_TYPE_TRACING programs which do not attach to other programs
	 * also need vmlinux BTF
	 */
	if (prog->type == BPF_PROG_TYPE_TRACING && !prog->attach_prog_fd)
		return true;

	return false;
}

static bool obj_needs_vmlinux_btf(const struct bpf_object *obj)
{
	struct bpf_program *prog;
	int i;

	/* CO-RE relocations need kernel BTF, only when btf_custom_path
	 * is not specified
	 */
	if (obj->btf_ext && obj->btf_ext->core_relo_info.len && !obj->btf_custom_path)
		return true;

	/* Support for typed ksyms needs kernel BTF */
	for (i = 0; i < obj->nr_extern; i++) {
		const struct extern_desc *ext;

		ext = &obj->externs[i];
		if (ext->type == EXT_KSYM && ext->ksym.type_id)
			return true;
	}

	bpf_object__for_each_program(prog, obj) {
		if (!prog->load)
			continue;
		if (prog_needs_vmlinux_btf(prog))
			return true;
	}

	return false;
}

static int bpf_object__load_vmlinux_btf(struct bpf_object *obj, bool force)
{
	int err;

	/* btf_vmlinux could be loaded earlier */
	if (obj->btf_vmlinux || obj->gen_loader)
		return 0;

	if (!force && !obj_needs_vmlinux_btf(obj))
		return 0;

	obj->btf_vmlinux = btf__load_vmlinux_btf();
	err = libbpf_get_error(obj->btf_vmlinux);
	if (err) {
		pr_warn("Error loading vmlinux BTF: %d\n", err);
		obj->btf_vmlinux = NULL;
		return err;
	}
	return 0;
}

static int bpf_object__sanitize_and_load_btf(struct bpf_object *obj)
{
	struct btf *kern_btf = obj->btf;
	bool btf_mandatory, sanitize;
	int i, err = 0;

	if (!obj->btf)
		return 0;

	if (!kernel_supports(obj, FEAT_BTF)) {
		if (kernel_needs_btf(obj)) {
			err = -EOPNOTSUPP;
			goto report;
		}
		pr_debug("Kernel doesn't support BTF, skipping uploading it.\n");
		return 0;
	}

	/* Even though some subprogs are global/weak, user might prefer more
	 * permissive BPF verification process that BPF verifier performs for
	 * static functions, taking into account more context from the caller
	 * functions. In such case, they need to mark such subprogs with
	 * __attribute__((visibility("hidden"))) and libbpf will adjust
	 * corresponding FUNC BTF type to be marked as static and trigger more
	 * involved BPF verification process.
	 */
	for (i = 0; i < obj->nr_programs; i++) {
		struct bpf_program *prog = &obj->programs[i];
		struct btf_type *t;
		const char *name;
		int j, n;

		if (!prog->mark_btf_static || !prog_is_subprog(obj, prog))
			continue;

		n = btf__get_nr_types(obj->btf);
		for (j = 1; j <= n; j++) {
			t = btf_type_by_id(obj->btf, j);
			if (!btf_is_func(t) || btf_func_linkage(t) != BTF_FUNC_GLOBAL)
				continue;

			name = btf__str_by_offset(obj->btf, t->name_off);
			if (strcmp(name, prog->name) != 0)
				continue;

			t->info = btf_type_info(BTF_KIND_FUNC, BTF_FUNC_STATIC, 0);
			break;
		}
	}

	sanitize = btf_needs_sanitization(obj);
	if (sanitize) {
		const void *raw_data;
		__u32 sz;

		/* clone BTF to sanitize a copy and leave the original intact */
		raw_data = btf__get_raw_data(obj->btf, &sz);
		kern_btf = btf__new(raw_data, sz);
		err = libbpf_get_error(kern_btf);
		if (err)
			return err;

		/* enforce 8-byte pointers for BPF-targeted BTFs */
		btf__set_pointer_size(obj->btf, 8);
		bpf_object__sanitize_btf(obj, kern_btf);
	}

	if (obj->gen_loader) {
		__u32 raw_size = 0;
		const void *raw_data = btf__get_raw_data(kern_btf, &raw_size);

		if (!raw_data)
			return -ENOMEM;
		bpf_gen__load_btf(obj->gen_loader, raw_data, raw_size);
		/* Pretend to have valid FD to pass various fd >= 0 checks.
		 * This fd == 0 will not be used with any syscall and will be reset to -1 eventually.
		 */
		btf__set_fd(kern_btf, 0);
	} else {
		err = btf__load_into_kernel(kern_btf);
	}
	if (sanitize) {
		if (!err) {
			/* move fd to libbpf's BTF */
			btf__set_fd(obj->btf, btf__fd(kern_btf));
			btf__set_fd(kern_btf, -1);
		}
		btf__free(kern_btf);
	}
report:
	if (err) {
		btf_mandatory = kernel_needs_btf(obj);
		pr_warn("Error loading .BTF into kernel: %d. %s\n", err,
			btf_mandatory ? "BTF is mandatory, can't proceed."
				      : "BTF is optional, ignoring.");
		if (!btf_mandatory)
			err = 0;
	}
	return err;
}

static const char *elf_sym_str(const struct bpf_object *obj, size_t off)
{
	const char *name;

	name = elf_strptr(obj->efile.elf, obj->efile.strtabidx, off);
	if (!name) {
		pr_warn("elf: failed to get section name string at offset %zu from %s: %s\n",
			off, obj->path, elf_errmsg(-1));
		return NULL;
	}

	return name;
}

static const char *elf_sec_str(const struct bpf_object *obj, size_t off)
{
	const char *name;

	name = elf_strptr(obj->efile.elf, obj->efile.shstrndx, off);
	if (!name) {
		pr_warn("elf: failed to get section name string at offset %zu from %s: %s\n",
			off, obj->path, elf_errmsg(-1));
		return NULL;
	}

	return name;
}

static Elf_Scn *elf_sec_by_idx(const struct bpf_object *obj, size_t idx)
{
	Elf_Scn *scn;

	scn = elf_getscn(obj->efile.elf, idx);
	if (!scn) {
		pr_warn("elf: failed to get section(%zu) from %s: %s\n",
			idx, obj->path, elf_errmsg(-1));
		return NULL;
	}
	return scn;
}

static Elf_Scn *elf_sec_by_name(const struct bpf_object *obj, const char *name)
{
	Elf_Scn *scn = NULL;
	Elf *elf = obj->efile.elf;
	const char *sec_name;

	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		sec_name = elf_sec_name(obj, scn);
		if (!sec_name)
			return NULL;

		if (strcmp(sec_name, name) != 0)
			continue;

		return scn;
	}
	return NULL;
}

static int elf_sec_hdr(const struct bpf_object *obj, Elf_Scn *scn, GElf_Shdr *hdr)
{
	if (!scn)
		return -EINVAL;

	if (gelf_getshdr(scn, hdr) != hdr) {
		pr_warn("elf: failed to get section(%zu) header from %s: %s\n",
			elf_ndxscn(scn), obj->path, elf_errmsg(-1));
		return -EINVAL;
	}

	return 0;
}

static const char *elf_sec_name(const struct bpf_object *obj, Elf_Scn *scn)
{
	const char *name;
	GElf_Shdr sh;

	if (!scn)
		return NULL;

	if (elf_sec_hdr(obj, scn, &sh))
		return NULL;

	name = elf_sec_str(obj, sh.sh_name);
	if (!name) {
		pr_warn("elf: failed to get section(%zu) name from %s: %s\n",
			elf_ndxscn(scn), obj->path, elf_errmsg(-1));
		return NULL;
	}

	return name;
}

static Elf_Data *elf_sec_data(const struct bpf_object *obj, Elf_Scn *scn)
{
	Elf_Data *data;

	if (!scn)
		return NULL;

	data = elf_getdata(scn, 0);
	if (!data) {
		pr_warn("elf: failed to get section(%zu) %s data from %s: %s\n",
			elf_ndxscn(scn), elf_sec_name(obj, scn) ?: "<?>",
			obj->path, elf_errmsg(-1));
		return NULL;
	}

	return data;
}

static bool is_sec_name_dwarf(const char *name)
{
	/* approximation, but the actual list is too long */
	return strncmp(name, ".debug_", sizeof(".debug_") - 1) == 0;
}

static bool ignore_elf_section(GElf_Shdr *hdr, const char *name)
{
	/* no special handling of .strtab */
	if (hdr->sh_type == SHT_STRTAB)
		return true;

	/* ignore .llvm_addrsig section as well */
	if (hdr->sh_type == SHT_LLVM_ADDRSIG)
		return true;

	/* no subprograms will lead to an empty .text section, ignore it */
	if (hdr->sh_type == SHT_PROGBITS && hdr->sh_size == 0 &&
	    strcmp(name, ".text") == 0)
		return true;

	/* DWARF sections */
	if (is_sec_name_dwarf(name))
		return true;

	if (strncmp(name, ".rel", sizeof(".rel") - 1) == 0) {
		name += sizeof(".rel") - 1;
		/* DWARF section relocations */
		if (is_sec_name_dwarf(name))
			return true;

		/* .BTF and .BTF.ext don't need relocations */
		if (strcmp(name, BTF_ELF_SEC) == 0 ||
		    strcmp(name, BTF_EXT_ELF_SEC) == 0)
			return true;
	}

	return false;
}

static int cmp_progs(const void *_a, const void *_b)
{
	const struct bpf_program *a = _a;
	const struct bpf_program *b = _b;

	if (a->sec_idx != b->sec_idx)
		return a->sec_idx < b->sec_idx ? -1 : 1;

	/* sec_insn_off can't be the same within the section */
	return a->sec_insn_off < b->sec_insn_off ? -1 : 1;
}

static int bpf_object__elf_collect(struct bpf_object *obj)
{
	Elf *elf = obj->efile.elf;
	Elf_Data *btf_ext_data = NULL;
	Elf_Data *btf_data = NULL;
	int idx = 0, err = 0;
	const char *name;
	Elf_Data *data;
	Elf_Scn *scn;
	GElf_Shdr sh;

	/* a bunch of ELF parsing functionality depends on processing symbols,
	 * so do the first pass and find the symbol table
	 */
	scn = NULL;
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		if (elf_sec_hdr(obj, scn, &sh))
			return -LIBBPF_ERRNO__FORMAT;

		if (sh.sh_type == SHT_SYMTAB) {
			if (obj->efile.symbols) {
				pr_warn("elf: multiple symbol tables in %s\n", obj->path);
				return -LIBBPF_ERRNO__FORMAT;
			}

			data = elf_sec_data(obj, scn);
			if (!data)
				return -LIBBPF_ERRNO__FORMAT;

			obj->efile.symbols = data;
			obj->efile.symbols_shndx = elf_ndxscn(scn);
			obj->efile.strtabidx = sh.sh_link;
		}
	}

	if (!obj->efile.symbols) {
		pr_warn("elf: couldn't find symbol table in %s, stripped object file?\n",
			obj->path);
		return -ENOENT;
	}

	scn = NULL;
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		idx++;

		if (elf_sec_hdr(obj, scn, &sh))
			return -LIBBPF_ERRNO__FORMAT;

		name = elf_sec_str(obj, sh.sh_name);
		if (!name)
			return -LIBBPF_ERRNO__FORMAT;

		if (ignore_elf_section(&sh, name))
			continue;

		data = elf_sec_data(obj, scn);
		if (!data)
			return -LIBBPF_ERRNO__FORMAT;

		pr_debug("elf: section(%d) %s, size %ld, link %d, flags %lx, type=%d\n",
			 idx, name, (unsigned long)data->d_size,
			 (int)sh.sh_link, (unsigned long)sh.sh_flags,
			 (int)sh.sh_type);

		if (strcmp(name, "license") == 0) {
			err = bpf_object__init_license(obj, data->d_buf, data->d_size);
			if (err)
				return err;
		} else if (strcmp(name, "version") == 0) {
			err = bpf_object__init_kversion(obj, data->d_buf, data->d_size);
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
			/* already processed during the first pass above */
		} else if (sh.sh_type == SHT_PROGBITS && data->d_size > 0) {
			if (sh.sh_flags & SHF_EXECINSTR) {
				if (strcmp(name, ".text") == 0)
					obj->efile.text_shndx = idx;
				err = bpf_object__add_programs(obj, data, name, idx);
				if (err)
					return err;
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
				pr_info("elf: skipping unrecognized data section(%d) %s\n",
					idx, name);
			}
		} else if (sh.sh_type == SHT_REL) {
			int nr_sects = obj->efile.nr_reloc_sects;
			void *sects = obj->efile.reloc_sects;
			int sec = sh.sh_info; /* points to other section */

			/* Only do relo for section with exec instructions */
			if (!section_have_execinstr(obj, sec) &&
			    strcmp(name, ".rel" STRUCT_OPS_SEC) &&
			    strcmp(name, ".rel" MAPS_ELF_SEC)) {
				pr_info("elf: skipping relo section(%d) %s for section(%d) %s\n",
					idx, name, sec,
					elf_sec_name(obj, elf_sec_by_idx(obj, sec)) ?: "<?>");
				continue;
			}

			sects = libbpf_reallocarray(sects, nr_sects + 1,
						    sizeof(*obj->efile.reloc_sects));
			if (!sects)
				return -ENOMEM;

			obj->efile.reloc_sects = sects;
			obj->efile.nr_reloc_sects++;

			obj->efile.reloc_sects[nr_sects].shdr = sh;
			obj->efile.reloc_sects[nr_sects].data = data;
		} else if (sh.sh_type == SHT_NOBITS && strcmp(name, BSS_SEC) == 0) {
			obj->efile.bss = data;
			obj->efile.bss_shndx = idx;
		} else {
			pr_info("elf: skipping section(%d) %s (size %zu)\n", idx, name,
				(size_t)sh.sh_size);
		}
	}

	if (!obj->efile.strtabidx || obj->efile.strtabidx > idx) {
		pr_warn("elf: symbol strings section missing or invalid in %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}

	/* sort BPF programs by section name and in-section instruction offset
	 * for faster search */
	qsort(obj->programs, obj->nr_programs, sizeof(*obj->programs), cmp_progs);

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

static bool sym_is_subprog(const GElf_Sym *sym, int text_shndx)
{
	int bind = GELF_ST_BIND(sym->st_info);
	int type = GELF_ST_TYPE(sym->st_info);

	/* in .text section */
	if (sym->st_shndx != text_shndx)
		return false;

	/* local function */
	if (bind == STB_LOCAL && type == STT_SECTION)
		return true;

	/* global function */
	return bind == STB_GLOBAL && type == STT_FUNC;
}

static int find_extern_btf_id(const struct btf *btf, const char *ext_name)
{
	const struct btf_type *t;
	const char *tname;
	int i, n;

	if (!btf)
		return -ESRCH;

	n = btf__get_nr_types(btf);
	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(btf, i);

		if (!btf_is_var(t) && !btf_is_func(t))
			continue;

		tname = btf__name_by_offset(btf, t->name_off);
		if (strcmp(tname, ext_name))
			continue;

		if (btf_is_var(t) &&
		    btf_var(t)->linkage != BTF_VAR_GLOBAL_EXTERN)
			return -EINVAL;

		if (btf_is_func(t) && btf_func_linkage(t) != BTF_FUNC_EXTERN)
			return -EINVAL;

		return i;
	}

	return -ENOENT;
}

static int find_extern_sec_btf_id(struct btf *btf, int ext_btf_id) {
	const struct btf_var_secinfo *vs;
	const struct btf_type *t;
	int i, j, n;

	if (!btf)
		return -ESRCH;

	n = btf__get_nr_types(btf);
	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(btf, i);

		if (!btf_is_datasec(t))
			continue;

		vs = btf_var_secinfos(t);
		for (j = 0; j < btf_vlen(t); j++, vs++) {
			if (vs->type == ext_btf_id)
				return i;
		}
	}

	return -ENOENT;
}

static enum kcfg_type find_kcfg_type(const struct btf *btf, int id,
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
			return t->size == 1 ? KCFG_BOOL : KCFG_UNKNOWN;
		if (is_signed)
			*is_signed = enc & BTF_INT_SIGNED;
		if (t->size == 1)
			return KCFG_CHAR;
		if (t->size < 1 || t->size > 8 || (t->size & (t->size - 1)))
			return KCFG_UNKNOWN;
		return KCFG_INT;
	}
	case BTF_KIND_ENUM:
		if (t->size != 4)
			return KCFG_UNKNOWN;
		if (strcmp(name, "libbpf_tristate"))
			return KCFG_UNKNOWN;
		return KCFG_TRISTATE;
	case BTF_KIND_ARRAY:
		if (btf_array(t)->nelems == 0)
			return KCFG_UNKNOWN;
		if (find_kcfg_type(btf, btf_array(t)->type, NULL) != KCFG_CHAR)
			return KCFG_UNKNOWN;
		return KCFG_CHAR_ARR;
	default:
		return KCFG_UNKNOWN;
	}
}

static int cmp_externs(const void *_a, const void *_b)
{
	const struct extern_desc *a = _a;
	const struct extern_desc *b = _b;

	if (a->type != b->type)
		return a->type < b->type ? -1 : 1;

	if (a->type == EXT_KCFG) {
		/* descending order by alignment requirements */
		if (a->kcfg.align != b->kcfg.align)
			return a->kcfg.align > b->kcfg.align ? -1 : 1;
		/* ascending order by size, within same alignment class */
		if (a->kcfg.sz != b->kcfg.sz)
			return a->kcfg.sz < b->kcfg.sz ? -1 : 1;
	}

	/* resolve ties by name */
	return strcmp(a->name, b->name);
}

static int find_int_btf_id(const struct btf *btf)
{
	const struct btf_type *t;
	int i, n;

	n = btf__get_nr_types(btf);
	for (i = 1; i <= n; i++) {
		t = btf__type_by_id(btf, i);

		if (btf_is_int(t) && btf_int_bits(t) == 32)
			return i;
	}

	return 0;
}

static int add_dummy_ksym_var(struct btf *btf)
{
	int i, int_btf_id, sec_btf_id, dummy_var_btf_id;
	const struct btf_var_secinfo *vs;
	const struct btf_type *sec;

	if (!btf)
		return 0;

	sec_btf_id = btf__find_by_name_kind(btf, KSYMS_SEC,
					    BTF_KIND_DATASEC);
	if (sec_btf_id < 0)
		return 0;

	sec = btf__type_by_id(btf, sec_btf_id);
	vs = btf_var_secinfos(sec);
	for (i = 0; i < btf_vlen(sec); i++, vs++) {
		const struct btf_type *vt;

		vt = btf__type_by_id(btf, vs->type);
		if (btf_is_func(vt))
			break;
	}

	/* No func in ksyms sec.  No need to add dummy var. */
	if (i == btf_vlen(sec))
		return 0;

	int_btf_id = find_int_btf_id(btf);
	dummy_var_btf_id = btf__add_var(btf,
					"dummy_ksym",
					BTF_VAR_GLOBAL_ALLOCATED,
					int_btf_id);
	if (dummy_var_btf_id < 0)
		pr_warn("cannot create a dummy_ksym var\n");

	return dummy_var_btf_id;
}

static int bpf_object__collect_externs(struct bpf_object *obj)
{
	struct btf_type *sec, *kcfg_sec = NULL, *ksym_sec = NULL;
	const struct btf_type *t;
	struct extern_desc *ext;
	int i, n, off, dummy_var_btf_id;
	const char *ext_name, *sec_name;
	Elf_Scn *scn;
	GElf_Shdr sh;

	if (!obj->efile.symbols)
		return 0;

	scn = elf_sec_by_idx(obj, obj->efile.symbols_shndx);
	if (elf_sec_hdr(obj, scn, &sh))
		return -LIBBPF_ERRNO__FORMAT;

	dummy_var_btf_id = add_dummy_ksym_var(obj->btf);
	if (dummy_var_btf_id < 0)
		return dummy_var_btf_id;

	n = sh.sh_size / sh.sh_entsize;
	pr_debug("looking for externs among %d symbols...\n", n);

	for (i = 0; i < n; i++) {
		GElf_Sym sym;

		if (!gelf_getsym(obj->efile.symbols, i, &sym))
			return -LIBBPF_ERRNO__FORMAT;
		if (!sym_is_extern(&sym))
			continue;
		ext_name = elf_sym_str(obj, sym.st_name);
		if (!ext_name || !ext_name[0])
			continue;

		ext = obj->externs;
		ext = libbpf_reallocarray(ext, obj->nr_extern + 1, sizeof(*ext));
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

		ext->sec_btf_id = find_extern_sec_btf_id(obj->btf, ext->btf_id);
		if (ext->sec_btf_id <= 0) {
			pr_warn("failed to find BTF for extern '%s' [%d] section: %d\n",
				ext_name, ext->btf_id, ext->sec_btf_id);
			return ext->sec_btf_id;
		}
		sec = (void *)btf__type_by_id(obj->btf, ext->sec_btf_id);
		sec_name = btf__name_by_offset(obj->btf, sec->name_off);

		if (strcmp(sec_name, KCONFIG_SEC) == 0) {
			if (btf_is_func(t)) {
				pr_warn("extern function %s is unsupported under %s section\n",
					ext->name, KCONFIG_SEC);
				return -ENOTSUP;
			}
			kcfg_sec = sec;
			ext->type = EXT_KCFG;
			ext->kcfg.sz = btf__resolve_size(obj->btf, t->type);
			if (ext->kcfg.sz <= 0) {
				pr_warn("failed to resolve size of extern (kcfg) '%s': %d\n",
					ext_name, ext->kcfg.sz);
				return ext->kcfg.sz;
			}
			ext->kcfg.align = btf__align_of(obj->btf, t->type);
			if (ext->kcfg.align <= 0) {
				pr_warn("failed to determine alignment of extern (kcfg) '%s': %d\n",
					ext_name, ext->kcfg.align);
				return -EINVAL;
			}
			ext->kcfg.type = find_kcfg_type(obj->btf, t->type,
						        &ext->kcfg.is_signed);
			if (ext->kcfg.type == KCFG_UNKNOWN) {
				pr_warn("extern (kcfg) '%s' type is unsupported\n", ext_name);
				return -ENOTSUP;
			}
		} else if (strcmp(sec_name, KSYMS_SEC) == 0) {
			if (btf_is_func(t) && ext->is_weak) {
				pr_warn("extern weak function %s is unsupported\n",
					ext->name);
				return -ENOTSUP;
			}
			ksym_sec = sec;
			ext->type = EXT_KSYM;
			skip_mods_and_typedefs(obj->btf, t->type,
					       &ext->ksym.type_id);
		} else {
			pr_warn("unrecognized extern section '%s'\n", sec_name);
			return -ENOTSUP;
		}
	}
	pr_debug("collected %d externs total\n", obj->nr_extern);

	if (!obj->nr_extern)
		return 0;

	/* sort externs by type, for kcfg ones also by (align, size, name) */
	qsort(obj->externs, obj->nr_extern, sizeof(*ext), cmp_externs);

	/* for .ksyms section, we need to turn all externs into allocated
	 * variables in BTF to pass kernel verification; we do this by
	 * pretending that each extern is a 8-byte variable
	 */
	if (ksym_sec) {
		/* find existing 4-byte integer type in BTF to use for fake
		 * extern variables in DATASEC
		 */
		int int_btf_id = find_int_btf_id(obj->btf);
		/* For extern function, a dummy_var added earlier
		 * will be used to replace the vs->type and
		 * its name string will be used to refill
		 * the missing param's name.
		 */
		const struct btf_type *dummy_var;

		dummy_var = btf__type_by_id(obj->btf, dummy_var_btf_id);
		for (i = 0; i < obj->nr_extern; i++) {
			ext = &obj->externs[i];
			if (ext->type != EXT_KSYM)
				continue;
			pr_debug("extern (ksym) #%d: symbol %d, name %s\n",
				 i, ext->sym_idx, ext->name);
		}

		sec = ksym_sec;
		n = btf_vlen(sec);
		for (i = 0, off = 0; i < n; i++, off += sizeof(int)) {
			struct btf_var_secinfo *vs = btf_var_secinfos(sec) + i;
			struct btf_type *vt;

			vt = (void *)btf__type_by_id(obj->btf, vs->type);
			ext_name = btf__name_by_offset(obj->btf, vt->name_off);
			ext = find_extern_by_name(obj, ext_name);
			if (!ext) {
				pr_warn("failed to find extern definition for BTF %s '%s'\n",
					btf_kind_str(vt), ext_name);
				return -ESRCH;
			}
			if (btf_is_func(vt)) {
				const struct btf_type *func_proto;
				struct btf_param *param;
				int j;

				func_proto = btf__type_by_id(obj->btf,
							     vt->type);
				param = btf_params(func_proto);
				/* Reuse the dummy_var string if the
				 * func proto does not have param name.
				 */
				for (j = 0; j < btf_vlen(func_proto); j++)
					if (param[j].type && !param[j].name_off)
						param[j].name_off =
							dummy_var->name_off;
				vs->type = dummy_var_btf_id;
				vt->info &= ~0xffff;
				vt->info |= BTF_FUNC_GLOBAL;
			} else {
				btf_var(vt)->linkage = BTF_VAR_GLOBAL_ALLOCATED;
				vt->type = int_btf_id;
			}
			vs->offset = off;
			vs->size = sizeof(int);
		}
		sec->size = off;
	}

	if (kcfg_sec) {
		sec = kcfg_sec;
		/* for kcfg externs calculate their offsets within a .kconfig map */
		off = 0;
		for (i = 0; i < obj->nr_extern; i++) {
			ext = &obj->externs[i];
			if (ext->type != EXT_KCFG)
				continue;

			ext->kcfg.data_off = roundup(off, ext->kcfg.align);
			off = ext->kcfg.data_off + ext->kcfg.sz;
			pr_debug("extern (kcfg) #%d: symbol %d, off %u, name %s\n",
				 i, ext->sym_idx, ext->kcfg.data_off, ext->name);
		}
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
			btf_var(t)->linkage = BTF_VAR_GLOBAL_ALLOCATED;
			vs->offset = ext->kcfg.data_off;
		}
	}
	return 0;
}

struct bpf_program *
bpf_object__find_program_by_title(const struct bpf_object *obj,
				  const char *title)
{
	struct bpf_program *pos;

	bpf_object__for_each_program(pos, obj) {
		if (pos->sec_name && !strcmp(pos->sec_name, title))
			return pos;
	}
	return errno = ENOENT, NULL;
}

static bool prog_is_subprog(const struct bpf_object *obj,
			    const struct bpf_program *prog)
{
	/* For legacy reasons, libbpf supports an entry-point BPF programs
	 * without SEC() attribute, i.e., those in the .text section. But if
	 * there are 2 or more such programs in the .text section, they all
	 * must be subprograms called from entry-point BPF programs in
	 * designated SEC()'tions, otherwise there is no way to distinguish
	 * which of those programs should be loaded vs which are a subprogram.
	 * Similarly, if there is a function/program in .text and at least one
	 * other BPF program with custom SEC() attribute, then we just assume
	 * .text programs are subprograms (even if they are not called from
	 * other programs), because libbpf never explicitly supported mixing
	 * SEC()-designated BPF programs and .text entry-point BPF programs.
	 */
	return prog->sec_idx == obj->efile.text_shndx && obj->nr_programs > 1;
}

struct bpf_program *
bpf_object__find_program_by_name(const struct bpf_object *obj,
				 const char *name)
{
	struct bpf_program *prog;

	bpf_object__for_each_program(prog, obj) {
		if (prog_is_subprog(obj, prog))
			continue;
		if (!strcmp(prog->name, name))
			return prog;
	}
	return errno = ENOENT, NULL;
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
				     __u32 insn_idx, const char *sym_name,
				     const GElf_Sym *sym, const GElf_Rel *rel)
{
	struct bpf_insn *insn = &prog->insns[insn_idx];
	size_t map_idx, nr_maps = prog->obj->nr_maps;
	struct bpf_object *obj = prog->obj;
	__u32 shdr_idx = sym->st_shndx;
	enum libbpf_map_type type;
	const char *sym_sec_name;
	struct bpf_map *map;

	if (!is_call_insn(insn) && !is_ldimm64_insn(insn)) {
		pr_warn("prog '%s': invalid relo against '%s' for insns[%d].code 0x%x\n",
			prog->name, sym_name, insn_idx, insn->code);
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
			pr_warn("prog '%s': extern relo failed to find extern for '%s' (%d)\n",
				prog->name, sym_name, sym_idx);
			return -LIBBPF_ERRNO__RELOC;
		}
		pr_debug("prog '%s': found extern #%d '%s' (sym %d) for insn #%u\n",
			 prog->name, i, ext->name, ext->sym_idx, insn_idx);
		if (insn->code == (BPF_JMP | BPF_CALL))
			reloc_desc->type = RELO_EXTERN_FUNC;
		else
			reloc_desc->type = RELO_EXTERN_VAR;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->sym_off = i; /* sym_off stores extern index */
		return 0;
	}

	/* sub-program call relocation */
	if (is_call_insn(insn)) {
		if (insn->src_reg != BPF_PSEUDO_CALL) {
			pr_warn("prog '%s': incorrect bpf_call opcode\n", prog->name);
			return -LIBBPF_ERRNO__RELOC;
		}
		/* text_shndx can be 0, if no default "main" program exists */
		if (!shdr_idx || shdr_idx != obj->efile.text_shndx) {
			sym_sec_name = elf_sec_name(obj, elf_sec_by_idx(obj, shdr_idx));
			pr_warn("prog '%s': bad call relo against '%s' in section '%s'\n",
				prog->name, sym_name, sym_sec_name);
			return -LIBBPF_ERRNO__RELOC;
		}
		if (sym->st_value % BPF_INSN_SZ) {
			pr_warn("prog '%s': bad call relo against '%s' at offset %zu\n",
				prog->name, sym_name, (size_t)sym->st_value);
			return -LIBBPF_ERRNO__RELOC;
		}
		reloc_desc->type = RELO_CALL;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->sym_off = sym->st_value;
		return 0;
	}

	if (!shdr_idx || shdr_idx >= SHN_LORESERVE) {
		pr_warn("prog '%s': invalid relo against '%s' in special section 0x%x; forgot to initialize global var?..\n",
			prog->name, sym_name, shdr_idx);
		return -LIBBPF_ERRNO__RELOC;
	}

	/* loading subprog addresses */
	if (sym_is_subprog(sym, obj->efile.text_shndx)) {
		/* global_func: sym->st_value = offset in the section, insn->imm = 0.
		 * local_func: sym->st_value = 0, insn->imm = offset in the section.
		 */
		if ((sym->st_value % BPF_INSN_SZ) || (insn->imm % BPF_INSN_SZ)) {
			pr_warn("prog '%s': bad subprog addr relo against '%s' at offset %zu+%d\n",
				prog->name, sym_name, (size_t)sym->st_value, insn->imm);
			return -LIBBPF_ERRNO__RELOC;
		}

		reloc_desc->type = RELO_SUBPROG_ADDR;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->sym_off = sym->st_value;
		return 0;
	}

	type = bpf_object__section_to_libbpf_map_type(obj, shdr_idx);
	sym_sec_name = elf_sec_name(obj, elf_sec_by_idx(obj, shdr_idx));

	/* generic map reference relocation */
	if (type == LIBBPF_MAP_UNSPEC) {
		if (!bpf_object__shndx_is_maps(obj, shdr_idx)) {
			pr_warn("prog '%s': bad map relo against '%s' in section '%s'\n",
				prog->name, sym_name, sym_sec_name);
			return -LIBBPF_ERRNO__RELOC;
		}
		for (map_idx = 0; map_idx < nr_maps; map_idx++) {
			map = &obj->maps[map_idx];
			if (map->libbpf_type != type ||
			    map->sec_idx != sym->st_shndx ||
			    map->sec_offset != sym->st_value)
				continue;
			pr_debug("prog '%s': found map %zd (%s, sec %d, off %zu) for insn #%u\n",
				 prog->name, map_idx, map->name, map->sec_idx,
				 map->sec_offset, insn_idx);
			break;
		}
		if (map_idx >= nr_maps) {
			pr_warn("prog '%s': map relo failed to find map for section '%s', off %zu\n",
				prog->name, sym_sec_name, (size_t)sym->st_value);
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
		pr_warn("prog '%s': bad data relo against section '%s'\n",
			prog->name, sym_sec_name);
		return -LIBBPF_ERRNO__RELOC;
	}
	for (map_idx = 0; map_idx < nr_maps; map_idx++) {
		map = &obj->maps[map_idx];
		if (map->libbpf_type != type)
			continue;
		pr_debug("prog '%s': found data map %zd (%s, sec %d, off %zu) for insn %u\n",
			 prog->name, map_idx, map->name, map->sec_idx,
			 map->sec_offset, insn_idx);
		break;
	}
	if (map_idx >= nr_maps) {
		pr_warn("prog '%s': data relo failed to find map for section '%s'\n",
			prog->name, sym_sec_name);
		return -LIBBPF_ERRNO__RELOC;
	}

	reloc_desc->type = RELO_DATA;
	reloc_desc->insn_idx = insn_idx;
	reloc_desc->map_idx = map_idx;
	reloc_desc->sym_off = sym->st_value;
	return 0;
}

static bool prog_contains_insn(const struct bpf_program *prog, size_t insn_idx)
{
	return insn_idx >= prog->sec_insn_off &&
	       insn_idx < prog->sec_insn_off + prog->sec_insn_cnt;
}

static struct bpf_program *find_prog_by_sec_insn(const struct bpf_object *obj,
						 size_t sec_idx, size_t insn_idx)
{
	int l = 0, r = obj->nr_programs - 1, m;
	struct bpf_program *prog;

	while (l < r) {
		m = l + (r - l + 1) / 2;
		prog = &obj->programs[m];

		if (prog->sec_idx < sec_idx ||
		    (prog->sec_idx == sec_idx && prog->sec_insn_off <= insn_idx))
			l = m;
		else
			r = m - 1;
	}
	/* matching program could be at index l, but it still might be the
	 * wrong one, so we need to double check conditions for the last time
	 */
	prog = &obj->programs[l];
	if (prog->sec_idx == sec_idx && prog_contains_insn(prog, insn_idx))
		return prog;
	return NULL;
}

static int
bpf_object__collect_prog_relos(struct bpf_object *obj, GElf_Shdr *shdr, Elf_Data *data)
{
	Elf_Data *symbols = obj->efile.symbols;
	const char *relo_sec_name, *sec_name;
	size_t sec_idx = shdr->sh_info;
	struct bpf_program *prog;
	struct reloc_desc *relos;
	int err, i, nrels;
	const char *sym_name;
	__u32 insn_idx;
	Elf_Scn *scn;
	Elf_Data *scn_data;
	GElf_Sym sym;
	GElf_Rel rel;

	scn = elf_sec_by_idx(obj, sec_idx);
	scn_data = elf_sec_data(obj, scn);

	relo_sec_name = elf_sec_str(obj, shdr->sh_name);
	sec_name = elf_sec_name(obj, scn);
	if (!relo_sec_name || !sec_name)
		return -EINVAL;

	pr_debug("sec '%s': collecting relocation for section(%zu) '%s'\n",
		 relo_sec_name, sec_idx, sec_name);
	nrels = shdr->sh_size / shdr->sh_entsize;

	for (i = 0; i < nrels; i++) {
		if (!gelf_getrel(data, i, &rel)) {
			pr_warn("sec '%s': failed to get relo #%d\n", relo_sec_name, i);
			return -LIBBPF_ERRNO__FORMAT;
		}
		if (!gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym)) {
			pr_warn("sec '%s': symbol 0x%zx not found for relo #%d\n",
				relo_sec_name, (size_t)GELF_R_SYM(rel.r_info), i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (rel.r_offset % BPF_INSN_SZ || rel.r_offset >= scn_data->d_size) {
			pr_warn("sec '%s': invalid offset 0x%zx for relo #%d\n",
				relo_sec_name, (size_t)GELF_R_SYM(rel.r_info), i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		insn_idx = rel.r_offset / BPF_INSN_SZ;
		/* relocations against static functions are recorded as
		 * relocations against the section that contains a function;
		 * in such case, symbol will be STT_SECTION and sym.st_name
		 * will point to empty string (0), so fetch section name
		 * instead
		 */
		if (GELF_ST_TYPE(sym.st_info) == STT_SECTION && sym.st_name == 0)
			sym_name = elf_sec_name(obj, elf_sec_by_idx(obj, sym.st_shndx));
		else
			sym_name = elf_sym_str(obj, sym.st_name);
		sym_name = sym_name ?: "<?";

		pr_debug("sec '%s': relo #%d: insn #%u against '%s'\n",
			 relo_sec_name, i, insn_idx, sym_name);

		prog = find_prog_by_sec_insn(obj, sec_idx, insn_idx);
		if (!prog) {
			pr_debug("sec '%s': relo #%d: couldn't find program in section '%s' for insn #%u, probably overridden weak function, skipping...\n",
				relo_sec_name, i, sec_name, insn_idx);
			continue;
		}

		relos = libbpf_reallocarray(prog->reloc_desc,
					    prog->nr_reloc + 1, sizeof(*relos));
		if (!relos)
			return -ENOMEM;
		prog->reloc_desc = relos;

		/* adjust insn_idx to local BPF program frame of reference */
		insn_idx -= prog->sec_insn_off;
		err = bpf_program__record_reloc(prog, &relos[prog->nr_reloc],
						insn_idx, sym_name, &sym, &rel);
		if (err)
			return err;

		prog->nr_reloc++;
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

static int bpf_get_map_info_from_fdinfo(int fd, struct bpf_map_info *info)
{
	char file[PATH_MAX], buff[4096];
	FILE *fp;
	__u32 val;
	int err;

	snprintf(file, sizeof(file), "/proc/%d/fdinfo/%d", getpid(), fd);
	memset(info, 0, sizeof(*info));

	fp = fopen(file, "r");
	if (!fp) {
		err = -errno;
		pr_warn("failed to open %s: %d. No procfs support?\n", file,
			err);
		return err;
	}

	while (fgets(buff, sizeof(buff), fp)) {
		if (sscanf(buff, "map_type:\t%u", &val) == 1)
			info->type = val;
		else if (sscanf(buff, "key_size:\t%u", &val) == 1)
			info->key_size = val;
		else if (sscanf(buff, "value_size:\t%u", &val) == 1)
			info->value_size = val;
		else if (sscanf(buff, "max_entries:\t%u", &val) == 1)
			info->max_entries = val;
		else if (sscanf(buff, "map_flags:\t%i", &val) == 1)
			info->map_flags = val;
	}

	fclose(fp);

	return 0;
}

int bpf_map__reuse_fd(struct bpf_map *map, int fd)
{
	struct bpf_map_info info = {};
	__u32 len = sizeof(info);
	int new_fd, err;
	char *new_name;

	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	if (err && errno == EINVAL)
		err = bpf_get_map_info_from_fdinfo(fd, &info);
	if (err)
		return libbpf_err(err);

	new_name = strdup(info.name);
	if (!new_name)
		return libbpf_err(-errno);

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
	return libbpf_err(err);
}

__u32 bpf_map__max_entries(const struct bpf_map *map)
{
	return map->def.max_entries;
}

struct bpf_map *bpf_map__inner_map(struct bpf_map *map)
{
	if (!bpf_map_type__is_map_in_map(map->def.type))
		return errno = EINVAL, NULL;

	return map->inner_map;
}

int bpf_map__set_max_entries(struct bpf_map *map, __u32 max_entries)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->def.max_entries = max_entries;
	return 0;
}

int bpf_map__resize(struct bpf_map *map, __u32 max_entries)
{
	if (!map || !max_entries)
		return libbpf_err(-EINVAL);

	return bpf_map__set_max_entries(map, max_entries);
}

static int
bpf_object__probe_loading(struct bpf_object *obj)
{
	struct bpf_load_program_attr attr;
	char *cp, errmsg[STRERR_BUFSIZE];
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int ret;

	if (obj->gen_loader)
		return 0;

	/* make sure basic loading works */

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insns = insns;
	attr.insns_cnt = ARRAY_SIZE(insns);
	attr.license = "GPL";

	ret = bpf_load_program_xattr(&attr, NULL, 0);
	if (ret < 0) {
		attr.prog_type = BPF_PROG_TYPE_TRACEPOINT;
		ret = bpf_load_program_xattr(&attr, NULL, 0);
	}
	if (ret < 0) {
		ret = errno;
		cp = libbpf_strerror_r(ret, errmsg, sizeof(errmsg));
		pr_warn("Error in %s():%s(%d). Couldn't load trivial BPF "
			"program. Make sure your kernel supports BPF "
			"(CONFIG_BPF_SYSCALL=y) and/or that RLIMIT_MEMLOCK is "
			"set to big enough value.\n", __func__, cp, ret);
		return -ret;
	}
	close(ret);

	return 0;
}

static int probe_fd(int fd)
{
	if (fd >= 0)
		close(fd);
	return fd >= 0;
}

static int probe_kern_prog_name(void)
{
	struct bpf_load_program_attr attr;
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int ret;

	/* make sure loading with name works */

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	attr.insns = insns;
	attr.insns_cnt = ARRAY_SIZE(insns);
	attr.license = "GPL";
	attr.name = "test";
	ret = bpf_load_program_xattr(&attr, NULL, 0);
	return probe_fd(ret);
}

static int probe_kern_global_data(void)
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
		ret = -errno;
		cp = libbpf_strerror_r(ret, errmsg, sizeof(errmsg));
		pr_warn("Error in %s():%s(%d). Couldn't create simple array map.\n",
			__func__, cp, -ret);
		return ret;
	}

	insns[0].imm = map;

	memset(&prg_attr, 0, sizeof(prg_attr));
	prg_attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	prg_attr.insns = insns;
	prg_attr.insns_cnt = ARRAY_SIZE(insns);
	prg_attr.license = "GPL";

	ret = bpf_load_program_xattr(&prg_attr, NULL, 0);
	close(map);
	return probe_fd(ret);
}

static int probe_kern_btf(void)
{
	static const char strs[] = "\0int";
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs)));
}

static int probe_kern_btf_func(void)
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

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs)));
}

static int probe_kern_btf_func_global(void)
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

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs)));
}

static int probe_kern_btf_datasec(void)
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

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs)));
}

static int probe_kern_btf_float(void)
{
	static const char strs[] = "\0float";
	__u32 types[] = {
		/* float */
		BTF_TYPE_FLOAT_ENC(1, 4),
	};

	return probe_fd(libbpf__load_raw_btf((char *)types, sizeof(types),
					     strs, sizeof(strs)));
}

static int probe_kern_array_mmap(void)
{
	struct bpf_create_map_attr attr = {
		.map_type = BPF_MAP_TYPE_ARRAY,
		.map_flags = BPF_F_MMAPABLE,
		.key_size = sizeof(int),
		.value_size = sizeof(int),
		.max_entries = 1,
	};

	return probe_fd(bpf_create_map_xattr(&attr));
}

static int probe_kern_exp_attach_type(void)
{
	struct bpf_load_program_attr attr;
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};

	memset(&attr, 0, sizeof(attr));
	/* use any valid combination of program type and (optional)
	 * non-zero expected attach type (i.e., not a BPF_CGROUP_INET_INGRESS)
	 * to see if kernel supports expected_attach_type field for
	 * BPF_PROG_LOAD command
	 */
	attr.prog_type = BPF_PROG_TYPE_CGROUP_SOCK;
	attr.expected_attach_type = BPF_CGROUP_INET_SOCK_CREATE;
	attr.insns = insns;
	attr.insns_cnt = ARRAY_SIZE(insns);
	attr.license = "GPL";

	return probe_fd(bpf_load_program_xattr(&attr, NULL, 0));
}

static int probe_kern_probe_read_kernel(void)
{
	struct bpf_load_program_attr attr;
	struct bpf_insn insns[] = {
		BPF_MOV64_REG(BPF_REG_1, BPF_REG_10),	/* r1 = r10 (fp) */
		BPF_ALU64_IMM(BPF_ADD, BPF_REG_1, -8),	/* r1 += -8 */
		BPF_MOV64_IMM(BPF_REG_2, 8),		/* r2 = 8 */
		BPF_MOV64_IMM(BPF_REG_3, 0),		/* r3 = 0 */
		BPF_RAW_INSN(BPF_JMP | BPF_CALL, 0, 0, 0, BPF_FUNC_probe_read_kernel),
		BPF_EXIT_INSN(),
	};

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_KPROBE;
	attr.insns = insns;
	attr.insns_cnt = ARRAY_SIZE(insns);
	attr.license = "GPL";

	return probe_fd(bpf_load_program_xattr(&attr, NULL, 0));
}

static int probe_prog_bind_map(void)
{
	struct bpf_load_program_attr prg_attr;
	struct bpf_create_map_attr map_attr;
	char *cp, errmsg[STRERR_BUFSIZE];
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int ret, map, prog;

	memset(&map_attr, 0, sizeof(map_attr));
	map_attr.map_type = BPF_MAP_TYPE_ARRAY;
	map_attr.key_size = sizeof(int);
	map_attr.value_size = 32;
	map_attr.max_entries = 1;

	map = bpf_create_map_xattr(&map_attr);
	if (map < 0) {
		ret = -errno;
		cp = libbpf_strerror_r(ret, errmsg, sizeof(errmsg));
		pr_warn("Error in %s():%s(%d). Couldn't create simple array map.\n",
			__func__, cp, -ret);
		return ret;
	}

	memset(&prg_attr, 0, sizeof(prg_attr));
	prg_attr.prog_type = BPF_PROG_TYPE_SOCKET_FILTER;
	prg_attr.insns = insns;
	prg_attr.insns_cnt = ARRAY_SIZE(insns);
	prg_attr.license = "GPL";

	prog = bpf_load_program_xattr(&prg_attr, NULL, 0);
	if (prog < 0) {
		close(map);
		return 0;
	}

	ret = bpf_prog_bind_map(prog, map, NULL);

	close(map);
	close(prog);

	return ret >= 0;
}

static int probe_module_btf(void)
{
	static const char strs[] = "\0int";
	__u32 types[] = {
		/* int */
		BTF_TYPE_INT_ENC(1, BTF_INT_SIGNED, 0, 32, 4),
	};
	struct bpf_btf_info info;
	__u32 len = sizeof(info);
	char name[16];
	int fd, err;

	fd = libbpf__load_raw_btf((char *)types, sizeof(types), strs, sizeof(strs));
	if (fd < 0)
		return 0; /* BTF not supported at all */

	memset(&info, 0, sizeof(info));
	info.name = ptr_to_u64(name);
	info.name_len = sizeof(name);

	/* check that BPF_OBJ_GET_INFO_BY_FD supports specifying name pointer;
	 * kernel's module BTF support coincides with support for
	 * name/name_len fields in struct bpf_btf_info.
	 */
	err = bpf_obj_get_info_by_fd(fd, &info, &len);
	close(fd);
	return !err;
}

static int probe_perf_link(void)
{
	struct bpf_load_program_attr attr;
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int prog_fd, link_fd, err;

	memset(&attr, 0, sizeof(attr));
	attr.prog_type = BPF_PROG_TYPE_TRACEPOINT;
	attr.insns = insns;
	attr.insns_cnt = ARRAY_SIZE(insns);
	attr.license = "GPL";
	prog_fd = bpf_load_program_xattr(&attr, NULL, 0);
	if (prog_fd < 0)
		return -errno;

	/* use invalid perf_event FD to get EBADF, if link is supported;
	 * otherwise EINVAL should be returned
	 */
	link_fd = bpf_link_create(prog_fd, -1, BPF_PERF_EVENT, NULL);
	err = -errno; /* close() can clobber errno */

	if (link_fd >= 0)
		close(link_fd);
	close(prog_fd);

	return link_fd < 0 && err == -EBADF;
}

enum kern_feature_result {
	FEAT_UNKNOWN = 0,
	FEAT_SUPPORTED = 1,
	FEAT_MISSING = 2,
};

typedef int (*feature_probe_fn)(void);

static struct kern_feature_desc {
	const char *desc;
	feature_probe_fn probe;
	enum kern_feature_result res;
} feature_probes[__FEAT_CNT] = {
	[FEAT_PROG_NAME] = {
		"BPF program name", probe_kern_prog_name,
	},
	[FEAT_GLOBAL_DATA] = {
		"global variables", probe_kern_global_data,
	},
	[FEAT_BTF] = {
		"minimal BTF", probe_kern_btf,
	},
	[FEAT_BTF_FUNC] = {
		"BTF functions", probe_kern_btf_func,
	},
	[FEAT_BTF_GLOBAL_FUNC] = {
		"BTF global function", probe_kern_btf_func_global,
	},
	[FEAT_BTF_DATASEC] = {
		"BTF data section and variable", probe_kern_btf_datasec,
	},
	[FEAT_ARRAY_MMAP] = {
		"ARRAY map mmap()", probe_kern_array_mmap,
	},
	[FEAT_EXP_ATTACH_TYPE] = {
		"BPF_PROG_LOAD expected_attach_type attribute",
		probe_kern_exp_attach_type,
	},
	[FEAT_PROBE_READ_KERN] = {
		"bpf_probe_read_kernel() helper", probe_kern_probe_read_kernel,
	},
	[FEAT_PROG_BIND_MAP] = {
		"BPF_PROG_BIND_MAP support", probe_prog_bind_map,
	},
	[FEAT_MODULE_BTF] = {
		"module BTF support", probe_module_btf,
	},
	[FEAT_BTF_FLOAT] = {
		"BTF_KIND_FLOAT support", probe_kern_btf_float,
	},
	[FEAT_PERF_LINK] = {
		"BPF perf link support", probe_perf_link,
	},
};

static bool kernel_supports(const struct bpf_object *obj, enum kern_feature_id feat_id)
{
	struct kern_feature_desc *feat = &feature_probes[feat_id];
	int ret;

	if (obj->gen_loader)
		/* To generate loader program assume the latest kernel
		 * to avoid doing extra prog_load, map_create syscalls.
		 */
		return true;

	if (READ_ONCE(feat->res) == FEAT_UNKNOWN) {
		ret = feat->probe();
		if (ret > 0) {
			WRITE_ONCE(feat->res, FEAT_SUPPORTED);
		} else if (ret == 0) {
			WRITE_ONCE(feat->res, FEAT_MISSING);
		} else {
			pr_warn("Detection of kernel %s support failed: %d\n", feat->desc, ret);
			WRITE_ONCE(feat->res, FEAT_MISSING);
		}
	}

	return READ_ONCE(feat->res) == FEAT_SUPPORTED;
}

static bool map_is_reuse_compat(const struct bpf_map *map, int map_fd)
{
	struct bpf_map_info map_info = {};
	char msg[STRERR_BUFSIZE];
	__u32 map_info_len;
	int err;

	map_info_len = sizeof(map_info);

	err = bpf_obj_get_info_by_fd(map_fd, &map_info, &map_info_len);
	if (err && errno == EINVAL)
		err = bpf_get_map_info_from_fdinfo(map_fd, &map_info);
	if (err) {
		pr_warn("failed to get map info for map FD %d: %s\n", map_fd,
			libbpf_strerror_r(errno, msg, sizeof(msg)));
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

	if (obj->gen_loader) {
		bpf_gen__map_update_elem(obj->gen_loader, map - obj->maps,
					 map->mmaped, map->def.value_size);
		if (map_type == LIBBPF_MAP_RODATA || map_type == LIBBPF_MAP_KCONFIG)
			bpf_gen__map_freeze(obj->gen_loader, map - obj->maps);
		return 0;
	}
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

static void bpf_map__destroy(struct bpf_map *map);

static int bpf_object__create_map(struct bpf_object *obj, struct bpf_map *map, bool is_inner)
{
	struct bpf_create_map_attr create_attr;
	struct bpf_map_def *def = &map->def;
	int err = 0;

	memset(&create_attr, 0, sizeof(create_attr));

	if (kernel_supports(obj, FEAT_PROG_NAME))
		create_attr.name = map->name;
	create_attr.map_ifindex = map->map_ifindex;
	create_attr.map_type = def->type;
	create_attr.map_flags = def->map_flags;
	create_attr.key_size = def->key_size;
	create_attr.value_size = def->value_size;
	create_attr.numa_node = map->numa_node;

	if (def->type == BPF_MAP_TYPE_PERF_EVENT_ARRAY && !def->max_entries) {
		int nr_cpus;

		nr_cpus = libbpf_num_possible_cpus();
		if (nr_cpus < 0) {
			pr_warn("map '%s': failed to determine number of system CPUs: %d\n",
				map->name, nr_cpus);
			return nr_cpus;
		}
		pr_debug("map '%s': setting size to %d\n", map->name, nr_cpus);
		create_attr.max_entries = nr_cpus;
	} else {
		create_attr.max_entries = def->max_entries;
	}

	if (bpf_map__is_struct_ops(map))
		create_attr.btf_vmlinux_value_type_id =
			map->btf_vmlinux_value_type_id;

	create_attr.btf_fd = 0;
	create_attr.btf_key_type_id = 0;
	create_attr.btf_value_type_id = 0;
	if (obj->btf && btf__fd(obj->btf) >= 0 && !bpf_map_find_btf_info(obj, map)) {
		create_attr.btf_fd = btf__fd(obj->btf);
		create_attr.btf_key_type_id = map->btf_key_type_id;
		create_attr.btf_value_type_id = map->btf_value_type_id;
	}

	if (bpf_map_type__is_map_in_map(def->type)) {
		if (map->inner_map) {
			err = bpf_object__create_map(obj, map->inner_map, true);
			if (err) {
				pr_warn("map '%s': failed to create inner map: %d\n",
					map->name, err);
				return err;
			}
			map->inner_map_fd = bpf_map__fd(map->inner_map);
		}
		if (map->inner_map_fd >= 0)
			create_attr.inner_map_fd = map->inner_map_fd;
	}

	if (obj->gen_loader) {
		bpf_gen__map_create(obj->gen_loader, &create_attr, is_inner ? -1 : map - obj->maps);
		/* Pretend to have valid FD to pass various fd >= 0 checks.
		 * This fd == 0 will not be used with any syscall and will be reset to -1 eventually.
		 */
		map->fd = 0;
	} else {
		map->fd = bpf_create_map_xattr(&create_attr);
	}
	if (map->fd < 0 && (create_attr.btf_key_type_id ||
			    create_attr.btf_value_type_id)) {
		char *cp, errmsg[STRERR_BUFSIZE];

		err = -errno;
		cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
		pr_warn("Error in bpf_create_map_xattr(%s):%s(%d). Retrying without BTF.\n",
			map->name, cp, err);
		create_attr.btf_fd = 0;
		create_attr.btf_key_type_id = 0;
		create_attr.btf_value_type_id = 0;
		map->btf_key_type_id = 0;
		map->btf_value_type_id = 0;
		map->fd = bpf_create_map_xattr(&create_attr);
	}

	err = map->fd < 0 ? -errno : 0;

	if (bpf_map_type__is_map_in_map(def->type) && map->inner_map) {
		if (obj->gen_loader)
			map->inner_map->fd = -1;
		bpf_map__destroy(map->inner_map);
		zfree(&map->inner_map);
	}

	return err;
}

static int init_map_slots(struct bpf_object *obj, struct bpf_map *map)
{
	const struct bpf_map *targ_map;
	unsigned int i;
	int fd, err = 0;

	for (i = 0; i < map->init_slots_sz; i++) {
		if (!map->init_slots[i])
			continue;

		targ_map = map->init_slots[i];
		fd = bpf_map__fd(targ_map);
		if (obj->gen_loader) {
			pr_warn("// TODO map_update_elem: idx %td key %d value==map_idx %td\n",
				map - obj->maps, i, targ_map - obj->maps);
			return -ENOTSUP;
		} else {
			err = bpf_map_update_elem(map->fd, &i, &fd, 0);
		}
		if (err) {
			err = -errno;
			pr_warn("map '%s': failed to initialize slot [%d] to map '%s' fd=%d: %d\n",
				map->name, i, targ_map->name,
				fd, err);
			return err;
		}
		pr_debug("map '%s': slot [%d] set to map '%s' fd=%d\n",
			 map->name, i, targ_map->name, fd);
	}

	zfree(&map->init_slots);
	map->init_slots_sz = 0;

	return 0;
}

static int
bpf_object__create_maps(struct bpf_object *obj)
{
	struct bpf_map *map;
	char *cp, errmsg[STRERR_BUFSIZE];
	unsigned int i, j;
	int err;
	bool retried;

	for (i = 0; i < obj->nr_maps; i++) {
		map = &obj->maps[i];

		retried = false;
retry:
		if (map->pin_path) {
			err = bpf_object__reuse_map(map);
			if (err) {
				pr_warn("map '%s': error reusing pinned map\n",
					map->name);
				goto err_out;
			}
			if (retried && map->fd < 0) {
				pr_warn("map '%s': cannot find pinned map\n",
					map->name);
				err = -ENOENT;
				goto err_out;
			}
		}

		if (map->fd >= 0) {
			pr_debug("map '%s': skipping creation (preset fd=%d)\n",
				 map->name, map->fd);
		} else {
			err = bpf_object__create_map(obj, map, false);
			if (err)
				goto err_out;

			pr_debug("map '%s': created successfully, fd=%d\n",
				 map->name, map->fd);

			if (bpf_map__is_internal(map)) {
				err = bpf_object__populate_internal_map(obj, map);
				if (err < 0) {
					zclose(map->fd);
					goto err_out;
				}
			}

			if (map->init_slots_sz) {
				err = init_map_slots(obj, map);
				if (err < 0) {
					zclose(map->fd);
					goto err_out;
				}
			}
		}

		if (map->pin_path && !map->pinned) {
			err = bpf_map__pin(map, NULL);
			if (err) {
				zclose(map->fd);
				if (!retried && err == -EEXIST) {
					retried = true;
					goto retry;
				}
				pr_warn("map '%s': failed to auto-pin at '%s': %d\n",
					map->name, map->pin_path, err);
				goto err_out;
			}
		}
	}

	return 0;

err_out:
	cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
	pr_warn("map '%s': failed to create: %s(%d)\n", map->name, cp, err);
	pr_perm_msg(err);
	for (j = 0; j < i; j++)
		zclose(obj->maps[j].fd);
	return err;
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
size_t bpf_core_essential_name_len(const char *name)
{
	size_t n = strlen(name);
	int i;

	for (i = n - 5; i >= 0; i--) {
		if (bpf_core_is_flavor_sep(name + i))
			return i + 1;
	}
	return n;
}

static void bpf_core_free_cands(struct bpf_core_cand_list *cands)
{
	free(cands->cands);
	free(cands);
}

static int bpf_core_add_cands(struct bpf_core_cand *local_cand,
			      size_t local_essent_len,
			      const struct btf *targ_btf,
			      const char *targ_btf_name,
			      int targ_start_id,
			      struct bpf_core_cand_list *cands)
{
	struct bpf_core_cand *new_cands, *cand;
	const struct btf_type *t;
	const char *targ_name;
	size_t targ_essent_len;
	int n, i;

	n = btf__get_nr_types(targ_btf);
	for (i = targ_start_id; i <= n; i++) {
		t = btf__type_by_id(targ_btf, i);
		if (btf_kind(t) != btf_kind(local_cand->t))
			continue;

		targ_name = btf__name_by_offset(targ_btf, t->name_off);
		if (str_is_empty(targ_name))
			continue;

		targ_essent_len = bpf_core_essential_name_len(targ_name);
		if (targ_essent_len != local_essent_len)
			continue;

		if (strncmp(local_cand->name, targ_name, local_essent_len) != 0)
			continue;

		pr_debug("CO-RE relocating [%d] %s %s: found target candidate [%d] %s %s in [%s]\n",
			 local_cand->id, btf_kind_str(local_cand->t),
			 local_cand->name, i, btf_kind_str(t), targ_name,
			 targ_btf_name);
		new_cands = libbpf_reallocarray(cands->cands, cands->len + 1,
					      sizeof(*cands->cands));
		if (!new_cands)
			return -ENOMEM;

		cand = &new_cands[cands->len];
		cand->btf = targ_btf;
		cand->t = t;
		cand->name = targ_name;
		cand->id = i;

		cands->cands = new_cands;
		cands->len++;
	}
	return 0;
}

static int load_module_btfs(struct bpf_object *obj)
{
	struct bpf_btf_info info;
	struct module_btf *mod_btf;
	struct btf *btf;
	char name[64];
	__u32 id = 0, len;
	int err, fd;

	if (obj->btf_modules_loaded)
		return 0;

	if (obj->gen_loader)
		return 0;

	/* don't do this again, even if we find no module BTFs */
	obj->btf_modules_loaded = true;

	/* kernel too old to support module BTFs */
	if (!kernel_supports(obj, FEAT_MODULE_BTF))
		return 0;

	while (true) {
		err = bpf_btf_get_next_id(id, &id);
		if (err && errno == ENOENT)
			return 0;
		if (err) {
			err = -errno;
			pr_warn("failed to iterate BTF objects: %d\n", err);
			return err;
		}

		fd = bpf_btf_get_fd_by_id(id);
		if (fd < 0) {
			if (errno == ENOENT)
				continue; /* expected race: BTF was unloaded */
			err = -errno;
			pr_warn("failed to get BTF object #%d FD: %d\n", id, err);
			return err;
		}

		len = sizeof(info);
		memset(&info, 0, sizeof(info));
		info.name = ptr_to_u64(name);
		info.name_len = sizeof(name);

		err = bpf_obj_get_info_by_fd(fd, &info, &len);
		if (err) {
			err = -errno;
			pr_warn("failed to get BTF object #%d info: %d\n", id, err);
			goto err_out;
		}

		/* ignore non-module BTFs */
		if (!info.kernel_btf || strcmp(name, "vmlinux") == 0) {
			close(fd);
			continue;
		}

		btf = btf_get_from_fd(fd, obj->btf_vmlinux);
		err = libbpf_get_error(btf);
		if (err) {
			pr_warn("failed to load module [%s]'s BTF object #%d: %d\n",
				name, id, err);
			goto err_out;
		}

		err = libbpf_ensure_mem((void **)&obj->btf_modules, &obj->btf_module_cap,
				        sizeof(*obj->btf_modules), obj->btf_module_cnt + 1);
		if (err)
			goto err_out;

		mod_btf = &obj->btf_modules[obj->btf_module_cnt++];

		mod_btf->btf = btf;
		mod_btf->id = id;
		mod_btf->fd = fd;
		mod_btf->name = strdup(name);
		if (!mod_btf->name) {
			err = -ENOMEM;
			goto err_out;
		}
		continue;

err_out:
		close(fd);
		return err;
	}

	return 0;
}

static struct bpf_core_cand_list *
bpf_core_find_cands(struct bpf_object *obj, const struct btf *local_btf, __u32 local_type_id)
{
	struct bpf_core_cand local_cand = {};
	struct bpf_core_cand_list *cands;
	const struct btf *main_btf;
	size_t local_essent_len;
	int err, i;

	local_cand.btf = local_btf;
	local_cand.t = btf__type_by_id(local_btf, local_type_id);
	if (!local_cand.t)
		return ERR_PTR(-EINVAL);

	local_cand.name = btf__name_by_offset(local_btf, local_cand.t->name_off);
	if (str_is_empty(local_cand.name))
		return ERR_PTR(-EINVAL);
	local_essent_len = bpf_core_essential_name_len(local_cand.name);

	cands = calloc(1, sizeof(*cands));
	if (!cands)
		return ERR_PTR(-ENOMEM);

	/* Attempt to find target candidates in vmlinux BTF first */
	main_btf = obj->btf_vmlinux_override ?: obj->btf_vmlinux;
	err = bpf_core_add_cands(&local_cand, local_essent_len, main_btf, "vmlinux", 1, cands);
	if (err)
		goto err_out;

	/* if vmlinux BTF has any candidate, don't got for module BTFs */
	if (cands->len)
		return cands;

	/* if vmlinux BTF was overridden, don't attempt to load module BTFs */
	if (obj->btf_vmlinux_override)
		return cands;

	/* now look through module BTFs, trying to still find candidates */
	err = load_module_btfs(obj);
	if (err)
		goto err_out;

	for (i = 0; i < obj->btf_module_cnt; i++) {
		err = bpf_core_add_cands(&local_cand, local_essent_len,
					 obj->btf_modules[i].btf,
					 obj->btf_modules[i].name,
					 btf__get_nr_types(obj->btf_vmlinux) + 1,
					 cands);
		if (err)
			goto err_out;
	}

	return cands;
err_out:
	bpf_core_free_cands(cands);
	return ERR_PTR(err);
}

/* Check local and target types for compatibility. This check is used for
 * type-based CO-RE relocations and follow slightly different rules than
 * field-based relocations. This function assumes that root types were already
 * checked for name match. Beyond that initial root-level name check, names
 * are completely ignored. Compatibility rules are as follows:
 *   - any two STRUCTs/UNIONs/FWDs/ENUMs/INTs are considered compatible, but
 *     kind should match for local and target types (i.e., STRUCT is not
 *     compatible with UNION);
 *   - for ENUMs, the size is ignored;
 *   - for INT, size and signedness are ignored;
 *   - for ARRAY, dimensionality is ignored, element types are checked for
 *     compatibility recursively;
 *   - CONST/VOLATILE/RESTRICT modifiers are ignored;
 *   - TYPEDEFs/PTRs are compatible if types they pointing to are compatible;
 *   - FUNC_PROTOs are compatible if they have compatible signature: same
 *     number of input args and compatible return and argument types.
 * These rules are not set in stone and probably will be adjusted as we get
 * more experience with using BPF CO-RE relocations.
 */
int bpf_core_types_are_compat(const struct btf *local_btf, __u32 local_id,
			      const struct btf *targ_btf, __u32 targ_id)
{
	const struct btf_type *local_type, *targ_type;
	int depth = 32; /* max recursion depth */

	/* caller made sure that names match (ignoring flavor suffix) */
	local_type = btf__type_by_id(local_btf, local_id);
	targ_type = btf__type_by_id(targ_btf, targ_id);
	if (btf_kind(local_type) != btf_kind(targ_type))
		return 0;

recur:
	depth--;
	if (depth < 0)
		return -EINVAL;

	local_type = skip_mods_and_typedefs(local_btf, local_id, &local_id);
	targ_type = skip_mods_and_typedefs(targ_btf, targ_id, &targ_id);
	if (!local_type || !targ_type)
		return -EINVAL;

	if (btf_kind(local_type) != btf_kind(targ_type))
		return 0;

	switch (btf_kind(local_type)) {
	case BTF_KIND_UNKN:
	case BTF_KIND_STRUCT:
	case BTF_KIND_UNION:
	case BTF_KIND_ENUM:
	case BTF_KIND_FWD:
		return 1;
	case BTF_KIND_INT:
		/* just reject deprecated bitfield-like integers; all other
		 * integers are by default compatible between each other
		 */
		return btf_int_offset(local_type) == 0 && btf_int_offset(targ_type) == 0;
	case BTF_KIND_PTR:
		local_id = local_type->type;
		targ_id = targ_type->type;
		goto recur;
	case BTF_KIND_ARRAY:
		local_id = btf_array(local_type)->type;
		targ_id = btf_array(targ_type)->type;
		goto recur;
	case BTF_KIND_FUNC_PROTO: {
		struct btf_param *local_p = btf_params(local_type);
		struct btf_param *targ_p = btf_params(targ_type);
		__u16 local_vlen = btf_vlen(local_type);
		__u16 targ_vlen = btf_vlen(targ_type);
		int i, err;

		if (local_vlen != targ_vlen)
			return 0;

		for (i = 0; i < local_vlen; i++, local_p++, targ_p++) {
			skip_mods_and_typedefs(local_btf, local_p->type, &local_id);
			skip_mods_and_typedefs(targ_btf, targ_p->type, &targ_id);
			err = bpf_core_types_are_compat(local_btf, local_id, targ_btf, targ_id);
			if (err <= 0)
				return err;
		}

		/* tail recurse for return type check */
		skip_mods_and_typedefs(local_btf, local_type->type, &local_id);
		skip_mods_and_typedefs(targ_btf, targ_type->type, &targ_id);
		goto recur;
	}
	default:
		pr_warn("unexpected kind %s relocated, local [%d], target [%d]\n",
			btf_kind_str(local_type), local_id, targ_id);
		return 0;
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

static int bpf_core_apply_relo(struct bpf_program *prog,
			       const struct bpf_core_relo *relo,
			       int relo_idx,
			       const struct btf *local_btf,
			       struct hashmap *cand_cache)
{
	const void *type_key = u32_as_hash_key(relo->type_id);
	struct bpf_core_cand_list *cands = NULL;
	const char *prog_name = prog->name;
	const struct btf_type *local_type;
	const char *local_name;
	__u32 local_id = relo->type_id;
	struct bpf_insn *insn;
	int insn_idx, err;

	if (relo->insn_off % BPF_INSN_SZ)
		return -EINVAL;
	insn_idx = relo->insn_off / BPF_INSN_SZ;
	/* adjust insn_idx from section frame of reference to the local
	 * program's frame of reference; (sub-)program code is not yet
	 * relocated, so it's enough to just subtract in-section offset
	 */
	insn_idx = insn_idx - prog->sec_insn_off;
	if (insn_idx > prog->insns_cnt)
		return -EINVAL;
	insn = &prog->insns[insn_idx];

	local_type = btf__type_by_id(local_btf, local_id);
	if (!local_type)
		return -EINVAL;

	local_name = btf__name_by_offset(local_btf, local_type->name_off);
	if (!local_name)
		return -EINVAL;

	if (prog->obj->gen_loader) {
		pr_warn("// TODO core_relo: prog %td insn[%d] %s kind %d\n",
			prog - prog->obj->programs, relo->insn_off / 8,
			local_name, relo->kind);
		return -ENOTSUP;
	}

	if (relo->kind != BPF_TYPE_ID_LOCAL &&
	    !hashmap__find(cand_cache, type_key, (void **)&cands)) {
		cands = bpf_core_find_cands(prog->obj, local_btf, local_id);
		if (IS_ERR(cands)) {
			pr_warn("prog '%s': relo #%d: target candidate search failed for [%d] %s %s: %ld\n",
				prog_name, relo_idx, local_id, btf_kind_str(local_type),
				local_name, PTR_ERR(cands));
			return PTR_ERR(cands);
		}
		err = hashmap__set(cand_cache, type_key, cands, NULL, NULL);
		if (err) {
			bpf_core_free_cands(cands);
			return err;
		}
	}

	return bpf_core_apply_relo_insn(prog_name, insn, insn_idx, relo, relo_idx, local_btf, cands);
}

static int
bpf_object__relocate_core(struct bpf_object *obj, const char *targ_btf_path)
{
	const struct btf_ext_info_sec *sec;
	const struct bpf_core_relo *rec;
	const struct btf_ext_info *seg;
	struct hashmap_entry *entry;
	struct hashmap *cand_cache = NULL;
	struct bpf_program *prog;
	const char *sec_name;
	int i, err = 0, insn_idx, sec_idx;

	if (obj->btf_ext->core_relo_info.len == 0)
		return 0;

	if (targ_btf_path) {
		obj->btf_vmlinux_override = btf__parse(targ_btf_path, NULL);
		err = libbpf_get_error(obj->btf_vmlinux_override);
		if (err) {
			pr_warn("failed to parse target BTF: %d\n", err);
			return err;
		}
	}

	cand_cache = hashmap__new(bpf_core_hash_fn, bpf_core_equal_fn, NULL);
	if (IS_ERR(cand_cache)) {
		err = PTR_ERR(cand_cache);
		goto out;
	}

	seg = &obj->btf_ext->core_relo_info;
	for_each_btf_ext_sec(seg, sec) {
		sec_name = btf__name_by_offset(obj->btf, sec->sec_name_off);
		if (str_is_empty(sec_name)) {
			err = -EINVAL;
			goto out;
		}
		/* bpf_object's ELF is gone by now so it's not easy to find
		 * section index by section name, but we can find *any*
		 * bpf_program within desired section name and use it's
		 * prog->sec_idx to do a proper search by section index and
		 * instruction offset
		 */
		prog = NULL;
		for (i = 0; i < obj->nr_programs; i++) {
			prog = &obj->programs[i];
			if (strcmp(prog->sec_name, sec_name) == 0)
				break;
		}
		if (!prog) {
			pr_warn("sec '%s': failed to find a BPF program\n", sec_name);
			return -ENOENT;
		}
		sec_idx = prog->sec_idx;

		pr_debug("sec '%s': found %d CO-RE relocations\n",
			 sec_name, sec->num_info);

		for_each_btf_ext_rec(seg, sec, i, rec) {
			insn_idx = rec->insn_off / BPF_INSN_SZ;
			prog = find_prog_by_sec_insn(obj, sec_idx, insn_idx);
			if (!prog) {
				pr_warn("sec '%s': failed to find program at insn #%d for CO-RE offset relocation #%d\n",
					sec_name, insn_idx, i);
				err = -EINVAL;
				goto out;
			}
			/* no need to apply CO-RE relocation if the program is
			 * not going to be loaded
			 */
			if (!prog->load)
				continue;

			err = bpf_core_apply_relo(prog, rec, i, obj->btf, cand_cache);
			if (err) {
				pr_warn("prog '%s': relo #%d: failed to relocate: %d\n",
					prog->name, i, err);
				goto out;
			}
		}
	}

out:
	/* obj->btf_vmlinux and module BTFs are freed after object load */
	btf__free(obj->btf_vmlinux_override);
	obj->btf_vmlinux_override = NULL;

	if (!IS_ERR_OR_NULL(cand_cache)) {
		hashmap__for_each_entry(cand_cache, entry, i) {
			bpf_core_free_cands(entry->value);
		}
		hashmap__free(cand_cache);
	}
	return err;
}

/* Relocate data references within program code:
 *  - map references;
 *  - global variable references;
 *  - extern references.
 */
static int
bpf_object__relocate_data(struct bpf_object *obj, struct bpf_program *prog)
{
	int i;

	for (i = 0; i < prog->nr_reloc; i++) {
		struct reloc_desc *relo = &prog->reloc_desc[i];
		struct bpf_insn *insn = &prog->insns[relo->insn_idx];
		struct extern_desc *ext;

		switch (relo->type) {
		case RELO_LD64:
			if (obj->gen_loader) {
				insn[0].src_reg = BPF_PSEUDO_MAP_IDX;
				insn[0].imm = relo->map_idx;
			} else {
				insn[0].src_reg = BPF_PSEUDO_MAP_FD;
				insn[0].imm = obj->maps[relo->map_idx].fd;
			}
			break;
		case RELO_DATA:
			insn[1].imm = insn[0].imm + relo->sym_off;
			if (obj->gen_loader) {
				insn[0].src_reg = BPF_PSEUDO_MAP_IDX_VALUE;
				insn[0].imm = relo->map_idx;
			} else {
				insn[0].src_reg = BPF_PSEUDO_MAP_VALUE;
				insn[0].imm = obj->maps[relo->map_idx].fd;
			}
			break;
		case RELO_EXTERN_VAR:
			ext = &obj->externs[relo->sym_off];
			if (ext->type == EXT_KCFG) {
				if (obj->gen_loader) {
					insn[0].src_reg = BPF_PSEUDO_MAP_IDX_VALUE;
					insn[0].imm = obj->kconfig_map_idx;
				} else {
					insn[0].src_reg = BPF_PSEUDO_MAP_VALUE;
					insn[0].imm = obj->maps[obj->kconfig_map_idx].fd;
				}
				insn[1].imm = ext->kcfg.data_off;
			} else /* EXT_KSYM */ {
				if (ext->ksym.type_id && ext->is_set) { /* typed ksyms */
					insn[0].src_reg = BPF_PSEUDO_BTF_ID;
					insn[0].imm = ext->ksym.kernel_btf_id;
					insn[1].imm = ext->ksym.kernel_btf_obj_fd;
				} else { /* typeless ksyms or unresolved typed ksyms */
					insn[0].imm = (__u32)ext->ksym.addr;
					insn[1].imm = ext->ksym.addr >> 32;
				}
			}
			break;
		case RELO_EXTERN_FUNC:
			ext = &obj->externs[relo->sym_off];
			insn[0].src_reg = BPF_PSEUDO_KFUNC_CALL;
			insn[0].imm = ext->ksym.kernel_btf_id;
			break;
		case RELO_SUBPROG_ADDR:
			if (insn[0].src_reg != BPF_PSEUDO_FUNC) {
				pr_warn("prog '%s': relo #%d: bad insn\n",
					prog->name, i);
				return -EINVAL;
			}
			/* handled already */
			break;
		case RELO_CALL:
			/* handled already */
			break;
		default:
			pr_warn("prog '%s': relo #%d: bad relo type %d\n",
				prog->name, i, relo->type);
			return -EINVAL;
		}
	}

	return 0;
}

static int adjust_prog_btf_ext_info(const struct bpf_object *obj,
				    const struct bpf_program *prog,
				    const struct btf_ext_info *ext_info,
				    void **prog_info, __u32 *prog_rec_cnt,
				    __u32 *prog_rec_sz)
{
	void *copy_start = NULL, *copy_end = NULL;
	void *rec, *rec_end, *new_prog_info;
	const struct btf_ext_info_sec *sec;
	size_t old_sz, new_sz;
	const char *sec_name;
	int i, off_adj;

	for_each_btf_ext_sec(ext_info, sec) {
		sec_name = btf__name_by_offset(obj->btf, sec->sec_name_off);
		if (!sec_name)
			return -EINVAL;
		if (strcmp(sec_name, prog->sec_name) != 0)
			continue;

		for_each_btf_ext_rec(ext_info, sec, i, rec) {
			__u32 insn_off = *(__u32 *)rec / BPF_INSN_SZ;

			if (insn_off < prog->sec_insn_off)
				continue;
			if (insn_off >= prog->sec_insn_off + prog->sec_insn_cnt)
				break;

			if (!copy_start)
				copy_start = rec;
			copy_end = rec + ext_info->rec_size;
		}

		if (!copy_start)
			return -ENOENT;

		/* append func/line info of a given (sub-)program to the main
		 * program func/line info
		 */
		old_sz = (size_t)(*prog_rec_cnt) * ext_info->rec_size;
		new_sz = old_sz + (copy_end - copy_start);
		new_prog_info = realloc(*prog_info, new_sz);
		if (!new_prog_info)
			return -ENOMEM;
		*prog_info = new_prog_info;
		*prog_rec_cnt = new_sz / ext_info->rec_size;
		memcpy(new_prog_info + old_sz, copy_start, copy_end - copy_start);

		/* Kernel instruction offsets are in units of 8-byte
		 * instructions, while .BTF.ext instruction offsets generated
		 * by Clang are in units of bytes. So convert Clang offsets
		 * into kernel offsets and adjust offset according to program
		 * relocated position.
		 */
		off_adj = prog->sub_insn_off - prog->sec_insn_off;
		rec = new_prog_info + old_sz;
		rec_end = new_prog_info + new_sz;
		for (; rec < rec_end; rec += ext_info->rec_size) {
			__u32 *insn_off = rec;

			*insn_off = *insn_off / BPF_INSN_SZ + off_adj;
		}
		*prog_rec_sz = ext_info->rec_size;
		return 0;
	}

	return -ENOENT;
}

static int
reloc_prog_func_and_line_info(const struct bpf_object *obj,
			      struct bpf_program *main_prog,
			      const struct bpf_program *prog)
{
	int err;

	/* no .BTF.ext relocation if .BTF.ext is missing or kernel doesn't
	 * supprot func/line info
	 */
	if (!obj->btf_ext || !kernel_supports(obj, FEAT_BTF_FUNC))
		return 0;

	/* only attempt func info relocation if main program's func_info
	 * relocation was successful
	 */
	if (main_prog != prog && !main_prog->func_info)
		goto line_info;

	err = adjust_prog_btf_ext_info(obj, prog, &obj->btf_ext->func_info,
				       &main_prog->func_info,
				       &main_prog->func_info_cnt,
				       &main_prog->func_info_rec_size);
	if (err) {
		if (err != -ENOENT) {
			pr_warn("prog '%s': error relocating .BTF.ext function info: %d\n",
				prog->name, err);
			return err;
		}
		if (main_prog->func_info) {
			/*
			 * Some info has already been found but has problem
			 * in the last btf_ext reloc. Must have to error out.
			 */
			pr_warn("prog '%s': missing .BTF.ext function info.\n", prog->name);
			return err;
		}
		/* Have problem loading the very first info. Ignore the rest. */
		pr_warn("prog '%s': missing .BTF.ext function info for the main program, skipping all of .BTF.ext func info.\n",
			prog->name);
	}

line_info:
	/* don't relocate line info if main program's relocation failed */
	if (main_prog != prog && !main_prog->line_info)
		return 0;

	err = adjust_prog_btf_ext_info(obj, prog, &obj->btf_ext->line_info,
				       &main_prog->line_info,
				       &main_prog->line_info_cnt,
				       &main_prog->line_info_rec_size);
	if (err) {
		if (err != -ENOENT) {
			pr_warn("prog '%s': error relocating .BTF.ext line info: %d\n",
				prog->name, err);
			return err;
		}
		if (main_prog->line_info) {
			/*
			 * Some info has already been found but has problem
			 * in the last btf_ext reloc. Must have to error out.
			 */
			pr_warn("prog '%s': missing .BTF.ext line info.\n", prog->name);
			return err;
		}
		/* Have problem loading the very first info. Ignore the rest. */
		pr_warn("prog '%s': missing .BTF.ext line info for the main program, skipping all of .BTF.ext line info.\n",
			prog->name);
	}
	return 0;
}

static int cmp_relo_by_insn_idx(const void *key, const void *elem)
{
	size_t insn_idx = *(const size_t *)key;
	const struct reloc_desc *relo = elem;

	if (insn_idx == relo->insn_idx)
		return 0;
	return insn_idx < relo->insn_idx ? -1 : 1;
}

static struct reloc_desc *find_prog_insn_relo(const struct bpf_program *prog, size_t insn_idx)
{
	return bsearch(&insn_idx, prog->reloc_desc, prog->nr_reloc,
		       sizeof(*prog->reloc_desc), cmp_relo_by_insn_idx);
}

static int append_subprog_relos(struct bpf_program *main_prog, struct bpf_program *subprog)
{
	int new_cnt = main_prog->nr_reloc + subprog->nr_reloc;
	struct reloc_desc *relos;
	int i;

	if (main_prog == subprog)
		return 0;
	relos = libbpf_reallocarray(main_prog->reloc_desc, new_cnt, sizeof(*relos));
	if (!relos)
		return -ENOMEM;
	memcpy(relos + main_prog->nr_reloc, subprog->reloc_desc,
	       sizeof(*relos) * subprog->nr_reloc);

	for (i = main_prog->nr_reloc; i < new_cnt; i++)
		relos[i].insn_idx += subprog->sub_insn_off;
	/* After insn_idx adjustment the 'relos' array is still sorted
	 * by insn_idx and doesn't break bsearch.
	 */
	main_prog->reloc_desc = relos;
	main_prog->nr_reloc = new_cnt;
	return 0;
}

static int
bpf_object__reloc_code(struct bpf_object *obj, struct bpf_program *main_prog,
		       struct bpf_program *prog)
{
	size_t sub_insn_idx, insn_idx, new_cnt;
	struct bpf_program *subprog;
	struct bpf_insn *insns, *insn;
	struct reloc_desc *relo;
	int err;

	err = reloc_prog_func_and_line_info(obj, main_prog, prog);
	if (err)
		return err;

	for (insn_idx = 0; insn_idx < prog->sec_insn_cnt; insn_idx++) {
		insn = &main_prog->insns[prog->sub_insn_off + insn_idx];
		if (!insn_is_subprog_call(insn) && !insn_is_pseudo_func(insn))
			continue;

		relo = find_prog_insn_relo(prog, insn_idx);
		if (relo && relo->type == RELO_EXTERN_FUNC)
			/* kfunc relocations will be handled later
			 * in bpf_object__relocate_data()
			 */
			continue;
		if (relo && relo->type != RELO_CALL && relo->type != RELO_SUBPROG_ADDR) {
			pr_warn("prog '%s': unexpected relo for insn #%zu, type %d\n",
				prog->name, insn_idx, relo->type);
			return -LIBBPF_ERRNO__RELOC;
		}
		if (relo) {
			/* sub-program instruction index is a combination of
			 * an offset of a symbol pointed to by relocation and
			 * call instruction's imm field; for global functions,
			 * call always has imm = -1, but for static functions
			 * relocation is against STT_SECTION and insn->imm
			 * points to a start of a static function
			 *
			 * for subprog addr relocation, the relo->sym_off + insn->imm is
			 * the byte offset in the corresponding section.
			 */
			if (relo->type == RELO_CALL)
				sub_insn_idx = relo->sym_off / BPF_INSN_SZ + insn->imm + 1;
			else
				sub_insn_idx = (relo->sym_off + insn->imm) / BPF_INSN_SZ;
		} else if (insn_is_pseudo_func(insn)) {
			/*
			 * RELO_SUBPROG_ADDR relo is always emitted even if both
			 * functions are in the same section, so it shouldn't reach here.
			 */
			pr_warn("prog '%s': missing subprog addr relo for insn #%zu\n",
				prog->name, insn_idx);
			return -LIBBPF_ERRNO__RELOC;
		} else {
			/* if subprogram call is to a static function within
			 * the same ELF section, there won't be any relocation
			 * emitted, but it also means there is no additional
			 * offset necessary, insns->imm is relative to
			 * instruction's original position within the section
			 */
			sub_insn_idx = prog->sec_insn_off + insn_idx + insn->imm + 1;
		}

		/* we enforce that sub-programs should be in .text section */
		subprog = find_prog_by_sec_insn(obj, obj->efile.text_shndx, sub_insn_idx);
		if (!subprog) {
			pr_warn("prog '%s': no .text section found yet sub-program call exists\n",
				prog->name);
			return -LIBBPF_ERRNO__RELOC;
		}

		/* if it's the first call instruction calling into this
		 * subprogram (meaning this subprog hasn't been processed
		 * yet) within the context of current main program:
		 *   - append it at the end of main program's instructions blog;
		 *   - process is recursively, while current program is put on hold;
		 *   - if that subprogram calls some other not yet processes
		 *   subprogram, same thing will happen recursively until
		 *   there are no more unprocesses subprograms left to append
		 *   and relocate.
		 */
		if (subprog->sub_insn_off == 0) {
			subprog->sub_insn_off = main_prog->insns_cnt;

			new_cnt = main_prog->insns_cnt + subprog->insns_cnt;
			insns = libbpf_reallocarray(main_prog->insns, new_cnt, sizeof(*insns));
			if (!insns) {
				pr_warn("prog '%s': failed to realloc prog code\n", main_prog->name);
				return -ENOMEM;
			}
			main_prog->insns = insns;
			main_prog->insns_cnt = new_cnt;

			memcpy(main_prog->insns + subprog->sub_insn_off, subprog->insns,
			       subprog->insns_cnt * sizeof(*insns));

			pr_debug("prog '%s': added %zu insns from sub-prog '%s'\n",
				 main_prog->name, subprog->insns_cnt, subprog->name);

			/* The subprog insns are now appended. Append its relos too. */
			err = append_subprog_relos(main_prog, subprog);
			if (err)
				return err;
			err = bpf_object__reloc_code(obj, main_prog, subprog);
			if (err)
				return err;
		}

		/* main_prog->insns memory could have been re-allocated, so
		 * calculate pointer again
		 */
		insn = &main_prog->insns[prog->sub_insn_off + insn_idx];
		/* calculate correct instruction position within current main
		 * prog; each main prog can have a different set of
		 * subprograms appended (potentially in different order as
		 * well), so position of any subprog can be different for
		 * different main programs */
		insn->imm = subprog->sub_insn_off - (prog->sub_insn_off + insn_idx) - 1;

		pr_debug("prog '%s': insn #%zu relocated, imm %d points to subprog '%s' (now at %zu offset)\n",
			 prog->name, insn_idx, insn->imm, subprog->name, subprog->sub_insn_off);
	}

	return 0;
}

/*
 * Relocate sub-program calls.
 *
 * Algorithm operates as follows. Each entry-point BPF program (referred to as
 * main prog) is processed separately. For each subprog (non-entry functions,
 * that can be called from either entry progs or other subprogs) gets their
 * sub_insn_off reset to zero. This serves as indicator that this subprogram
 * hasn't been yet appended and relocated within current main prog. Once its
 * relocated, sub_insn_off will point at the position within current main prog
 * where given subprog was appended. This will further be used to relocate all
 * the call instructions jumping into this subprog.
 *
 * We start with main program and process all call instructions. If the call
 * is into a subprog that hasn't been processed (i.e., subprog->sub_insn_off
 * is zero), subprog instructions are appended at the end of main program's
 * instruction array. Then main program is "put on hold" while we recursively
 * process newly appended subprogram. If that subprogram calls into another
 * subprogram that hasn't been appended, new subprogram is appended again to
 * the *main* prog's instructions (subprog's instructions are always left
 * untouched, as they need to be in unmodified state for subsequent main progs
 * and subprog instructions are always sent only as part of a main prog) and
 * the process continues recursively. Once all the subprogs called from a main
 * prog or any of its subprogs are appended (and relocated), all their
 * positions within finalized instructions array are known, so it's easy to
 * rewrite call instructions with correct relative offsets, corresponding to
 * desired target subprog.
 *
 * Its important to realize that some subprogs might not be called from some
 * main prog and any of its called/used subprogs. Those will keep their
 * subprog->sub_insn_off as zero at all times and won't be appended to current
 * main prog and won't be relocated within the context of current main prog.
 * They might still be used from other main progs later.
 *
 * Visually this process can be shown as below. Suppose we have two main
 * programs mainA and mainB and BPF object contains three subprogs: subA,
 * subB, and subC. mainA calls only subA, mainB calls only subC, but subA and
 * subC both call subB:
 *
 *        +--------+ +-------+
 *        |        v v       |
 *     +--+---+ +--+-+-+ +---+--+
 *     | subA | | subB | | subC |
 *     +--+---+ +------+ +---+--+
 *        ^                  ^
 *        |                  |
 *    +---+-------+   +------+----+
 *    |   mainA   |   |   mainB   |
 *    +-----------+   +-----------+
 *
 * We'll start relocating mainA, will find subA, append it and start
 * processing sub A recursively:
 *
 *    +-----------+------+
 *    |   mainA   | subA |
 *    +-----------+------+
 *
 * At this point we notice that subB is used from subA, so we append it and
 * relocate (there are no further subcalls from subB):
 *
 *    +-----------+------+------+
 *    |   mainA   | subA | subB |
 *    +-----------+------+------+
 *
 * At this point, we relocate subA calls, then go one level up and finish with
 * relocatin mainA calls. mainA is done.
 *
 * For mainB process is similar but results in different order. We start with
 * mainB and skip subA and subB, as mainB never calls them (at least
 * directly), but we see subC is needed, so we append and start processing it:
 *
 *    +-----------+------+
 *    |   mainB   | subC |
 *    +-----------+------+
 * Now we see subC needs subB, so we go back to it, append and relocate it:
 *
 *    +-----------+------+------+
 *    |   mainB   | subC | subB |
 *    +-----------+------+------+
 *
 * At this point we unwind recursion, relocate calls in subC, then in mainB.
 */
static int
bpf_object__relocate_calls(struct bpf_object *obj, struct bpf_program *prog)
{
	struct bpf_program *subprog;
	int i, err;

	/* mark all subprogs as not relocated (yet) within the context of
	 * current main program
	 */
	for (i = 0; i < obj->nr_programs; i++) {
		subprog = &obj->programs[i];
		if (!prog_is_subprog(obj, subprog))
			continue;

		subprog->sub_insn_off = 0;
	}

	err = bpf_object__reloc_code(obj, prog, prog);
	if (err)
		return err;


	return 0;
}

static void
bpf_object__free_relocs(struct bpf_object *obj)
{
	struct bpf_program *prog;
	int i;

	/* free up relocation descriptors */
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		zfree(&prog->reloc_desc);
		prog->nr_reloc = 0;
	}
}

static int
bpf_object__relocate(struct bpf_object *obj, const char *targ_btf_path)
{
	struct bpf_program *prog;
	size_t i, j;
	int err;

	if (obj->btf_ext) {
		err = bpf_object__relocate_core(obj, targ_btf_path);
		if (err) {
			pr_warn("failed to perform CO-RE relocations: %d\n",
				err);
			return err;
		}
	}

	/* Before relocating calls pre-process relocations and mark
	 * few ld_imm64 instructions that points to subprogs.
	 * Otherwise bpf_object__reloc_code() later would have to consider
	 * all ld_imm64 insns as relocation candidates. That would
	 * reduce relocation speed, since amount of find_prog_insn_relo()
	 * would increase and most of them will fail to find a relo.
	 */
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		for (j = 0; j < prog->nr_reloc; j++) {
			struct reloc_desc *relo = &prog->reloc_desc[j];
			struct bpf_insn *insn = &prog->insns[relo->insn_idx];

			/* mark the insn, so it's recognized by insn_is_pseudo_func() */
			if (relo->type == RELO_SUBPROG_ADDR)
				insn[0].src_reg = BPF_PSEUDO_FUNC;
		}
	}

	/* relocate subprogram calls and append used subprograms to main
	 * programs; each copy of subprogram code needs to be relocated
	 * differently for each main program, because its code location might
	 * have changed.
	 * Append subprog relos to main programs to allow data relos to be
	 * processed after text is completely relocated.
	 */
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		/* sub-program's sub-calls are relocated within the context of
		 * its main program only
		 */
		if (prog_is_subprog(obj, prog))
			continue;

		err = bpf_object__relocate_calls(obj, prog);
		if (err) {
			pr_warn("prog '%s': failed to relocate calls: %d\n",
				prog->name, err);
			return err;
		}
	}
	/* Process data relos for main programs */
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog_is_subprog(obj, prog))
			continue;
		err = bpf_object__relocate_data(obj, prog);
		if (err) {
			pr_warn("prog '%s': failed to relocate data references: %d\n",
				prog->name, err);
			return err;
		}
	}
	if (!obj->gen_loader)
		bpf_object__free_relocs(obj);
	return 0;
}

static int bpf_object__collect_st_ops_relos(struct bpf_object *obj,
					    GElf_Shdr *shdr, Elf_Data *data);

static int bpf_object__collect_map_relos(struct bpf_object *obj,
					 GElf_Shdr *shdr, Elf_Data *data)
{
	const int bpf_ptr_sz = 8, host_ptr_sz = sizeof(void *);
	int i, j, nrels, new_sz;
	const struct btf_var_secinfo *vi = NULL;
	const struct btf_type *sec, *var, *def;
	struct bpf_map *map = NULL, *targ_map;
	const struct btf_member *member;
	const char *name, *mname;
	Elf_Data *symbols;
	unsigned int moff;
	GElf_Sym sym;
	GElf_Rel rel;
	void *tmp;

	if (!obj->efile.btf_maps_sec_btf_id || !obj->btf)
		return -EINVAL;
	sec = btf__type_by_id(obj->btf, obj->efile.btf_maps_sec_btf_id);
	if (!sec)
		return -EINVAL;

	symbols = obj->efile.symbols;
	nrels = shdr->sh_size / shdr->sh_entsize;
	for (i = 0; i < nrels; i++) {
		if (!gelf_getrel(data, i, &rel)) {
			pr_warn(".maps relo #%d: failed to get ELF relo\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}
		if (!gelf_getsym(symbols, GELF_R_SYM(rel.r_info), &sym)) {
			pr_warn(".maps relo #%d: symbol %zx not found\n",
				i, (size_t)GELF_R_SYM(rel.r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}
		name = elf_sym_str(obj, sym.st_name) ?: "<?>";
		if (sym.st_shndx != obj->efile.btf_maps_shndx) {
			pr_warn(".maps relo #%d: '%s' isn't a BTF-defined map\n",
				i, name);
			return -LIBBPF_ERRNO__RELOC;
		}

		pr_debug(".maps relo #%d: for %zd value %zd rel.r_offset %zu name %d ('%s')\n",
			 i, (ssize_t)(rel.r_info >> 32), (size_t)sym.st_value,
			 (size_t)rel.r_offset, sym.st_name, name);

		for (j = 0; j < obj->nr_maps; j++) {
			map = &obj->maps[j];
			if (map->sec_idx != obj->efile.btf_maps_shndx)
				continue;

			vi = btf_var_secinfos(sec) + map->btf_var_idx;
			if (vi->offset <= rel.r_offset &&
			    rel.r_offset + bpf_ptr_sz <= vi->offset + vi->size)
				break;
		}
		if (j == obj->nr_maps) {
			pr_warn(".maps relo #%d: cannot find map '%s' at rel.r_offset %zu\n",
				i, name, (size_t)rel.r_offset);
			return -EINVAL;
		}

		if (!bpf_map_type__is_map_in_map(map->def.type))
			return -EINVAL;
		if (map->def.type == BPF_MAP_TYPE_HASH_OF_MAPS &&
		    map->def.key_size != sizeof(int)) {
			pr_warn(".maps relo #%d: hash-of-maps '%s' should have key size %zu.\n",
				i, map->name, sizeof(int));
			return -EINVAL;
		}

		targ_map = bpf_object__find_map_by_name(obj, name);
		if (!targ_map)
			return -ESRCH;

		var = btf__type_by_id(obj->btf, vi->type);
		def = skip_mods_and_typedefs(obj->btf, var->type, NULL);
		if (btf_vlen(def) == 0)
			return -EINVAL;
		member = btf_members(def) + btf_vlen(def) - 1;
		mname = btf__name_by_offset(obj->btf, member->name_off);
		if (strcmp(mname, "values"))
			return -EINVAL;

		moff = btf_member_bit_offset(def, btf_vlen(def) - 1) / 8;
		if (rel.r_offset - vi->offset < moff)
			return -EINVAL;

		moff = rel.r_offset - vi->offset - moff;
		/* here we use BPF pointer size, which is always 64 bit, as we
		 * are parsing ELF that was built for BPF target
		 */
		if (moff % bpf_ptr_sz)
			return -EINVAL;
		moff /= bpf_ptr_sz;
		if (moff >= map->init_slots_sz) {
			new_sz = moff + 1;
			tmp = libbpf_reallocarray(map->init_slots, new_sz, host_ptr_sz);
			if (!tmp)
				return -ENOMEM;
			map->init_slots = tmp;
			memset(map->init_slots + map->init_slots_sz, 0,
			       (new_sz - map->init_slots_sz) * host_ptr_sz);
			map->init_slots_sz = new_sz;
		}
		map->init_slots[moff] = targ_map;

		pr_debug(".maps relo #%d: map '%s' slot [%d] points to map '%s'\n",
			 i, map->name, moff, name);
	}

	return 0;
}

static int cmp_relocs(const void *_a, const void *_b)
{
	const struct reloc_desc *a = _a;
	const struct reloc_desc *b = _b;

	if (a->insn_idx != b->insn_idx)
		return a->insn_idx < b->insn_idx ? -1 : 1;

	/* no two relocations should have the same insn_idx, but ... */
	if (a->type != b->type)
		return a->type < b->type ? -1 : 1;

	return 0;
}

static int bpf_object__collect_relos(struct bpf_object *obj)
{
	int i, err;

	for (i = 0; i < obj->efile.nr_reloc_sects; i++) {
		GElf_Shdr *shdr = &obj->efile.reloc_sects[i].shdr;
		Elf_Data *data = obj->efile.reloc_sects[i].data;
		int idx = shdr->sh_info;

		if (shdr->sh_type != SHT_REL) {
			pr_warn("internal error at %d\n", __LINE__);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		if (idx == obj->efile.st_ops_shndx)
			err = bpf_object__collect_st_ops_relos(obj, shdr, data);
		else if (idx == obj->efile.btf_maps_shndx)
			err = bpf_object__collect_map_relos(obj, shdr, data);
		else
			err = bpf_object__collect_prog_relos(obj, shdr, data);
		if (err)
			return err;
	}

	for (i = 0; i < obj->nr_programs; i++) {
		struct bpf_program *p = &obj->programs[i];

		if (!p->nr_reloc)
			continue;

		qsort(p->reloc_desc, p->nr_reloc, sizeof(*p->reloc_desc), cmp_relocs);
	}
	return 0;
}

static bool insn_is_helper_call(struct bpf_insn *insn, enum bpf_func_id *func_id)
{
	if (BPF_CLASS(insn->code) == BPF_JMP &&
	    BPF_OP(insn->code) == BPF_CALL &&
	    BPF_SRC(insn->code) == BPF_K &&
	    insn->src_reg == 0 &&
	    insn->dst_reg == 0) {
		    *func_id = insn->imm;
		    return true;
	}
	return false;
}

static int bpf_object__sanitize_prog(struct bpf_object *obj, struct bpf_program *prog)
{
	struct bpf_insn *insn = prog->insns;
	enum bpf_func_id func_id;
	int i;

	if (obj->gen_loader)
		return 0;

	for (i = 0; i < prog->insns_cnt; i++, insn++) {
		if (!insn_is_helper_call(insn, &func_id))
			continue;

		/* on kernels that don't yet support
		 * bpf_probe_read_{kernel,user}[_str] helpers, fall back
		 * to bpf_probe_read() which works well for old kernels
		 */
		switch (func_id) {
		case BPF_FUNC_probe_read_kernel:
		case BPF_FUNC_probe_read_user:
			if (!kernel_supports(obj, FEAT_PROBE_READ_KERN))
				insn->imm = BPF_FUNC_probe_read;
			break;
		case BPF_FUNC_probe_read_kernel_str:
		case BPF_FUNC_probe_read_user_str:
			if (!kernel_supports(obj, FEAT_PROBE_READ_KERN))
				insn->imm = BPF_FUNC_probe_read_str;
			break;
		default:
			break;
		}
	}
	return 0;
}

static int
load_program(struct bpf_program *prog, struct bpf_insn *insns, int insns_cnt,
	     char *license, __u32 kern_version, int *pfd)
{
	struct bpf_prog_load_params load_attr = {};
	char *cp, errmsg[STRERR_BUFSIZE];
	size_t log_buf_size = 0;
	char *log_buf = NULL;
	int btf_fd, ret;

	if (prog->type == BPF_PROG_TYPE_UNSPEC) {
		/*
		 * The program type must be set.  Most likely we couldn't find a proper
		 * section definition at load time, and thus we didn't infer the type.
		 */
		pr_warn("prog '%s': missing BPF prog type, check ELF section name '%s'\n",
			prog->name, prog->sec_name);
		return -EINVAL;
	}

	if (!insns || !insns_cnt)
		return -EINVAL;

	load_attr.prog_type = prog->type;
	/* old kernels might not support specifying expected_attach_type */
	if (!kernel_supports(prog->obj, FEAT_EXP_ATTACH_TYPE) && prog->sec_def &&
	    prog->sec_def->is_exp_attach_type_optional)
		load_attr.expected_attach_type = 0;
	else
		load_attr.expected_attach_type = prog->expected_attach_type;
	if (kernel_supports(prog->obj, FEAT_PROG_NAME))
		load_attr.name = prog->name;
	load_attr.insns = insns;
	load_attr.insn_cnt = insns_cnt;
	load_attr.license = license;
	load_attr.attach_btf_id = prog->attach_btf_id;
	if (prog->attach_prog_fd)
		load_attr.attach_prog_fd = prog->attach_prog_fd;
	else
		load_attr.attach_btf_obj_fd = prog->attach_btf_obj_fd;
	load_attr.attach_btf_id = prog->attach_btf_id;
	load_attr.kern_version = kern_version;
	load_attr.prog_ifindex = prog->prog_ifindex;

	/* specify func_info/line_info only if kernel supports them */
	btf_fd = bpf_object__btf_fd(prog->obj);
	if (btf_fd >= 0 && kernel_supports(prog->obj, FEAT_BTF_FUNC)) {
		load_attr.prog_btf_fd = btf_fd;
		load_attr.func_info = prog->func_info;
		load_attr.func_info_rec_size = prog->func_info_rec_size;
		load_attr.func_info_cnt = prog->func_info_cnt;
		load_attr.line_info = prog->line_info;
		load_attr.line_info_rec_size = prog->line_info_rec_size;
		load_attr.line_info_cnt = prog->line_info_cnt;
	}
	load_attr.log_level = prog->log_level;
	load_attr.prog_flags = prog->prog_flags;

	if (prog->obj->gen_loader) {
		bpf_gen__prog_load(prog->obj->gen_loader, &load_attr,
				   prog - prog->obj->programs);
		*pfd = -1;
		return 0;
	}
retry_load:
	if (log_buf_size) {
		log_buf = malloc(log_buf_size);
		if (!log_buf)
			return -ENOMEM;

		*log_buf = 0;
	}

	load_attr.log_buf = log_buf;
	load_attr.log_buf_sz = log_buf_size;
	ret = libbpf__bpf_prog_load(&load_attr);

	if (ret >= 0) {
		if (log_buf && load_attr.log_level)
			pr_debug("verifier log:\n%s", log_buf);

		if (prog->obj->rodata_map_idx >= 0 &&
		    kernel_supports(prog->obj, FEAT_PROG_BIND_MAP)) {
			struct bpf_map *rodata_map =
				&prog->obj->maps[prog->obj->rodata_map_idx];

			if (bpf_prog_bind_map(ret, bpf_map__fd(rodata_map), NULL)) {
				cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
				pr_warn("prog '%s': failed to bind .rodata map: %s\n",
					prog->name, cp);
				/* Don't fail hard if can't bind rodata. */
			}
		}

		*pfd = ret;
		ret = 0;
		goto out;
	}

	if (!log_buf || errno == ENOSPC) {
		log_buf_size = max((size_t)BPF_LOG_BUF_SIZE,
				   log_buf_size << 1);

		free(log_buf);
		goto retry_load;
	}
	ret = errno ? -errno : -LIBBPF_ERRNO__LOAD;
	cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
	pr_warn("load bpf program failed: %s\n", cp);
	pr_perm_msg(ret);

	if (log_buf && log_buf[0] != '\0') {
		ret = -LIBBPF_ERRNO__VERIFY;
		pr_warn("-- BEGIN DUMP LOG ---\n");
		pr_warn("\n%s\n", log_buf);
		pr_warn("-- END LOG --\n");
	} else if (load_attr.insn_cnt >= BPF_MAXINSNS) {
		pr_warn("Program too large (%zu insns), at most %d insns\n",
			load_attr.insn_cnt, BPF_MAXINSNS);
		ret = -LIBBPF_ERRNO__PROG2BIG;
	} else if (load_attr.prog_type != BPF_PROG_TYPE_KPROBE) {
		/* Wrong program type? */
		int fd;

		load_attr.prog_type = BPF_PROG_TYPE_KPROBE;
		load_attr.expected_attach_type = 0;
		load_attr.log_buf = NULL;
		load_attr.log_buf_sz = 0;
		fd = libbpf__bpf_prog_load(&load_attr);
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

static int bpf_program__record_externs(struct bpf_program *prog)
{
	struct bpf_object *obj = prog->obj;
	int i;

	for (i = 0; i < prog->nr_reloc; i++) {
		struct reloc_desc *relo = &prog->reloc_desc[i];
		struct extern_desc *ext = &obj->externs[relo->sym_off];

		switch (relo->type) {
		case RELO_EXTERN_VAR:
			if (ext->type != EXT_KSYM)
				continue;
			if (!ext->ksym.type_id) {
				pr_warn("typeless ksym %s is not supported yet\n",
					ext->name);
				return -ENOTSUP;
			}
			bpf_gen__record_extern(obj->gen_loader, ext->name, BTF_KIND_VAR,
					       relo->insn_idx);
			break;
		case RELO_EXTERN_FUNC:
			bpf_gen__record_extern(obj->gen_loader, ext->name, BTF_KIND_FUNC,
					       relo->insn_idx);
			break;
		default:
			continue;
		}
	}
	return 0;
}

static int libbpf_find_attach_btf_id(struct bpf_program *prog, int *btf_obj_fd, int *btf_type_id);

int bpf_program__load(struct bpf_program *prog, char *license, __u32 kern_ver)
{
	int err = 0, fd, i;

	if (prog->obj->loaded) {
		pr_warn("prog '%s': can't load after object was loaded\n", prog->name);
		return libbpf_err(-EINVAL);
	}

	if ((prog->type == BPF_PROG_TYPE_TRACING ||
	     prog->type == BPF_PROG_TYPE_LSM ||
	     prog->type == BPF_PROG_TYPE_EXT) && !prog->attach_btf_id) {
		int btf_obj_fd = 0, btf_type_id = 0;

		err = libbpf_find_attach_btf_id(prog, &btf_obj_fd, &btf_type_id);
		if (err)
			return libbpf_err(err);

		prog->attach_btf_obj_fd = btf_obj_fd;
		prog->attach_btf_id = btf_type_id;
	}

	if (prog->instances.nr < 0 || !prog->instances.fds) {
		if (prog->preprocessor) {
			pr_warn("Internal error: can't load program '%s'\n",
				prog->name);
			return libbpf_err(-LIBBPF_ERRNO__INTERNAL);
		}

		prog->instances.fds = malloc(sizeof(int));
		if (!prog->instances.fds) {
			pr_warn("Not enough memory for BPF fds\n");
			return libbpf_err(-ENOMEM);
		}
		prog->instances.nr = 1;
		prog->instances.fds[0] = -1;
	}

	if (!prog->preprocessor) {
		if (prog->instances.nr != 1) {
			pr_warn("prog '%s': inconsistent nr(%d) != 1\n",
				prog->name, prog->instances.nr);
		}
		if (prog->obj->gen_loader)
			bpf_program__record_externs(prog);
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
				i, prog->name);
			goto out;
		}

		if (!result.new_insn_ptr || !result.new_insn_cnt) {
			pr_debug("Skip loading the %dth instance of program '%s'\n",
				 i, prog->name);
			prog->instances.fds[i] = -1;
			if (result.pfd)
				*result.pfd = -1;
			continue;
		}

		err = load_program(prog, result.new_insn_ptr,
				   result.new_insn_cnt, license, kern_ver, &fd);
		if (err) {
			pr_warn("Loading the %dth instance of program '%s' failed\n",
				i, prog->name);
			goto out;
		}

		if (result.pfd)
			*result.pfd = fd;
		prog->instances.fds[i] = fd;
	}
out:
	if (err)
		pr_warn("failed to load program '%s'\n", prog->name);
	zfree(&prog->insns);
	prog->insns_cnt = 0;
	return libbpf_err(err);
}

static int
bpf_object__load_progs(struct bpf_object *obj, int log_level)
{
	struct bpf_program *prog;
	size_t i;
	int err;

	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		err = bpf_object__sanitize_prog(obj, prog);
		if (err)
			return err;
	}

	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog_is_subprog(obj, prog))
			continue;
		if (!prog->load) {
			pr_debug("prog '%s': skipped loading\n", prog->name);
			continue;
		}
		prog->log_level |= log_level;
		err = bpf_program__load(prog, obj->license, obj->kern_version);
		if (err)
			return err;
	}
	if (obj->gen_loader)
		bpf_object__free_relocs(obj);
	return 0;
}

static const struct bpf_sec_def *find_sec_def(const char *sec_name);

static int bpf_object_init_progs(struct bpf_object *obj, const struct bpf_object_open_opts *opts)
{
	struct bpf_program *prog;

	bpf_object__for_each_program(prog, obj) {
		prog->sec_def = find_sec_def(prog->sec_name);
		if (!prog->sec_def) {
			/* couldn't guess, but user might manually specify */
			pr_debug("prog '%s': unrecognized ELF section name '%s'\n",
				prog->name, prog->sec_name);
			continue;
		}

		if (prog->sec_def->is_sleepable)
			prog->prog_flags |= BPF_F_SLEEPABLE;
		bpf_program__set_type(prog, prog->sec_def->prog_type);
		bpf_program__set_expected_attach_type(prog, prog->sec_def->expected_attach_type);

		if (prog->sec_def->prog_type == BPF_PROG_TYPE_TRACING ||
		    prog->sec_def->prog_type == BPF_PROG_TYPE_EXT)
			prog->attach_prog_fd = OPTS_GET(opts, attach_prog_fd, 0);
	}

	return 0;
}

static struct bpf_object *
__bpf_object__open(const char *path, const void *obj_buf, size_t obj_buf_sz,
		   const struct bpf_object_open_opts *opts)
{
	const char *obj_name, *kconfig, *btf_tmp_path;
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

	btf_tmp_path = OPTS_GET(opts, btf_custom_path, NULL);
	if (btf_tmp_path) {
		if (strlen(btf_tmp_path) >= PATH_MAX) {
			err = -ENAMETOOLONG;
			goto out;
		}
		obj->btf_custom_path = strdup(btf_tmp_path);
		if (!obj->btf_custom_path) {
			err = -ENOMEM;
			goto out;
		}
	}

	kconfig = OPTS_GET(opts, kconfig, NULL);
	if (kconfig) {
		obj->kconfig = strdup(kconfig);
		if (!obj->kconfig) {
			err = -ENOMEM;
			goto out;
		}
	}

	err = bpf_object__elf_init(obj);
	err = err ? : bpf_object__check_endianness(obj);
	err = err ? : bpf_object__elf_collect(obj);
	err = err ? : bpf_object__collect_externs(obj);
	err = err ? : bpf_object__finalize_btf(obj);
	err = err ? : bpf_object__init_maps(obj, opts);
	err = err ? : bpf_object_init_progs(obj, opts);
	err = err ? : bpf_object__collect_relos(obj);
	if (err)
		goto out;

	bpf_object__elf_finish(obj);

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
	return libbpf_ptr(__bpf_object__open_xattr(attr, 0));
}

struct bpf_object *bpf_object__open(const char *path)
{
	struct bpf_object_open_attr attr = {
		.file		= path,
		.prog_type	= BPF_PROG_TYPE_UNSPEC,
	};

	return libbpf_ptr(__bpf_object__open_xattr(&attr, 0));
}

struct bpf_object *
bpf_object__open_file(const char *path, const struct bpf_object_open_opts *opts)
{
	if (!path)
		return libbpf_err_ptr(-EINVAL);

	pr_debug("loading %s\n", path);

	return libbpf_ptr(__bpf_object__open(path, NULL, 0, opts));
}

struct bpf_object *
bpf_object__open_mem(const void *obj_buf, size_t obj_buf_sz,
		     const struct bpf_object_open_opts *opts)
{
	if (!obj_buf || obj_buf_sz == 0)
		return libbpf_err_ptr(-EINVAL);

	return libbpf_ptr(__bpf_object__open(NULL, obj_buf, obj_buf_sz, opts));
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
		return errno = EINVAL, NULL;

	return libbpf_ptr(__bpf_object__open(NULL, obj_buf, obj_buf_sz, &opts));
}

int bpf_object__unload(struct bpf_object *obj)
{
	size_t i;

	if (!obj)
		return libbpf_err(-EINVAL);

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
		if (!kernel_supports(obj, FEAT_GLOBAL_DATA)) {
			pr_warn("kernel doesn't support global data\n");
			return -ENOTSUP;
		}
		if (!kernel_supports(obj, FEAT_ARRAY_MMAP))
			m->def.map_flags ^= BPF_F_MMAPABLE;
	}

	return 0;
}

static int bpf_object__read_kallsyms_file(struct bpf_object *obj)
{
	char sym_type, sym_name[500];
	unsigned long long sym_addr;
	const struct btf_type *t;
	struct extern_desc *ext;
	int ret, err = 0;
	FILE *f;

	f = fopen("/proc/kallsyms", "r");
	if (!f) {
		err = -errno;
		pr_warn("failed to open /proc/kallsyms: %d\n", err);
		return err;
	}

	while (true) {
		ret = fscanf(f, "%llx %c %499s%*[^\n]\n",
			     &sym_addr, &sym_type, sym_name);
		if (ret == EOF && feof(f))
			break;
		if (ret != 3) {
			pr_warn("failed to read kallsyms entry: %d\n", ret);
			err = -EINVAL;
			goto out;
		}

		ext = find_extern_by_name(obj, sym_name);
		if (!ext || ext->type != EXT_KSYM)
			continue;

		t = btf__type_by_id(obj->btf, ext->btf_id);
		if (!btf_is_var(t))
			continue;

		if (ext->is_set && ext->ksym.addr != sym_addr) {
			pr_warn("extern (ksym) '%s' resolution is ambiguous: 0x%llx or 0x%llx\n",
				sym_name, ext->ksym.addr, sym_addr);
			err = -EINVAL;
			goto out;
		}
		if (!ext->is_set) {
			ext->is_set = true;
			ext->ksym.addr = sym_addr;
			pr_debug("extern (ksym) %s=0x%llx\n", sym_name, sym_addr);
		}
	}

out:
	fclose(f);
	return err;
}

static int find_ksym_btf_id(struct bpf_object *obj, const char *ksym_name,
			    __u16 kind, struct btf **res_btf,
			    int *res_btf_fd)
{
	int i, id, btf_fd, err;
	struct btf *btf;

	btf = obj->btf_vmlinux;
	btf_fd = 0;
	id = btf__find_by_name_kind(btf, ksym_name, kind);

	if (id == -ENOENT) {
		err = load_module_btfs(obj);
		if (err)
			return err;

		for (i = 0; i < obj->btf_module_cnt; i++) {
			btf = obj->btf_modules[i].btf;
			/* we assume module BTF FD is always >0 */
			btf_fd = obj->btf_modules[i].fd;
			id = btf__find_by_name_kind(btf, ksym_name, kind);
			if (id != -ENOENT)
				break;
		}
	}
	if (id <= 0)
		return -ESRCH;

	*res_btf = btf;
	*res_btf_fd = btf_fd;
	return id;
}

static int bpf_object__resolve_ksym_var_btf_id(struct bpf_object *obj,
					       struct extern_desc *ext)
{
	const struct btf_type *targ_var, *targ_type;
	__u32 targ_type_id, local_type_id;
	const char *targ_var_name;
	int id, btf_fd = 0, err;
	struct btf *btf = NULL;

	id = find_ksym_btf_id(obj, ext->name, BTF_KIND_VAR, &btf, &btf_fd);
	if (id == -ESRCH && ext->is_weak) {
		return 0;
	} else if (id < 0) {
		pr_warn("extern (var ksym) '%s': not found in kernel BTF\n",
			ext->name);
		return id;
	}

	/* find local type_id */
	local_type_id = ext->ksym.type_id;

	/* find target type_id */
	targ_var = btf__type_by_id(btf, id);
	targ_var_name = btf__name_by_offset(btf, targ_var->name_off);
	targ_type = skip_mods_and_typedefs(btf, targ_var->type, &targ_type_id);

	err = bpf_core_types_are_compat(obj->btf, local_type_id,
					btf, targ_type_id);
	if (err <= 0) {
		const struct btf_type *local_type;
		const char *targ_name, *local_name;

		local_type = btf__type_by_id(obj->btf, local_type_id);
		local_name = btf__name_by_offset(obj->btf, local_type->name_off);
		targ_name = btf__name_by_offset(btf, targ_type->name_off);

		pr_warn("extern (var ksym) '%s': incompatible types, expected [%d] %s %s, but kernel has [%d] %s %s\n",
			ext->name, local_type_id,
			btf_kind_str(local_type), local_name, targ_type_id,
			btf_kind_str(targ_type), targ_name);
		return -EINVAL;
	}

	ext->is_set = true;
	ext->ksym.kernel_btf_obj_fd = btf_fd;
	ext->ksym.kernel_btf_id = id;
	pr_debug("extern (var ksym) '%s': resolved to [%d] %s %s\n",
		 ext->name, id, btf_kind_str(targ_var), targ_var_name);

	return 0;
}

static int bpf_object__resolve_ksym_func_btf_id(struct bpf_object *obj,
						struct extern_desc *ext)
{
	int local_func_proto_id, kfunc_proto_id, kfunc_id;
	const struct btf_type *kern_func;
	struct btf *kern_btf = NULL;
	int ret, kern_btf_fd = 0;

	local_func_proto_id = ext->ksym.type_id;

	kfunc_id = find_ksym_btf_id(obj, ext->name, BTF_KIND_FUNC,
				    &kern_btf, &kern_btf_fd);
	if (kfunc_id < 0) {
		pr_warn("extern (func ksym) '%s': not found in kernel BTF\n",
			ext->name);
		return kfunc_id;
	}

	if (kern_btf != obj->btf_vmlinux) {
		pr_warn("extern (func ksym) '%s': function in kernel module is not supported\n",
			ext->name);
		return -ENOTSUP;
	}

	kern_func = btf__type_by_id(kern_btf, kfunc_id);
	kfunc_proto_id = kern_func->type;

	ret = bpf_core_types_are_compat(obj->btf, local_func_proto_id,
					kern_btf, kfunc_proto_id);
	if (ret <= 0) {
		pr_warn("extern (func ksym) '%s': func_proto [%d] incompatible with kernel [%d]\n",
			ext->name, local_func_proto_id, kfunc_proto_id);
		return -EINVAL;
	}

	ext->is_set = true;
	ext->ksym.kernel_btf_obj_fd = kern_btf_fd;
	ext->ksym.kernel_btf_id = kfunc_id;
	pr_debug("extern (func ksym) '%s': resolved to kernel [%d]\n",
		 ext->name, kfunc_id);

	return 0;
}

static int bpf_object__resolve_ksyms_btf_id(struct bpf_object *obj)
{
	const struct btf_type *t;
	struct extern_desc *ext;
	int i, err;

	for (i = 0; i < obj->nr_extern; i++) {
		ext = &obj->externs[i];
		if (ext->type != EXT_KSYM || !ext->ksym.type_id)
			continue;

		if (obj->gen_loader) {
			ext->is_set = true;
			ext->ksym.kernel_btf_obj_fd = 0;
			ext->ksym.kernel_btf_id = 0;
			continue;
		}
		t = btf__type_by_id(obj->btf, ext->btf_id);
		if (btf_is_var(t))
			err = bpf_object__resolve_ksym_var_btf_id(obj, ext);
		else
			err = bpf_object__resolve_ksym_func_btf_id(obj, ext);
		if (err)
			return err;
	}
	return 0;
}

static int bpf_object__resolve_externs(struct bpf_object *obj,
				       const char *extra_kconfig)
{
	bool need_config = false, need_kallsyms = false;
	bool need_vmlinux_btf = false;
	struct extern_desc *ext;
	void *kcfg_data = NULL;
	int err, i;

	if (obj->nr_extern == 0)
		return 0;

	if (obj->kconfig_map_idx >= 0)
		kcfg_data = obj->maps[obj->kconfig_map_idx].mmaped;

	for (i = 0; i < obj->nr_extern; i++) {
		ext = &obj->externs[i];

		if (ext->type == EXT_KCFG &&
		    strcmp(ext->name, "LINUX_KERNEL_VERSION") == 0) {
			void *ext_val = kcfg_data + ext->kcfg.data_off;
			__u32 kver = get_kernel_version();

			if (!kver) {
				pr_warn("failed to get kernel version\n");
				return -EINVAL;
			}
			err = set_kcfg_value_num(ext, ext_val, kver);
			if (err)
				return err;
			pr_debug("extern (kcfg) %s=0x%x\n", ext->name, kver);
		} else if (ext->type == EXT_KCFG &&
			   strncmp(ext->name, "CONFIG_", 7) == 0) {
			need_config = true;
		} else if (ext->type == EXT_KSYM) {
			if (ext->ksym.type_id)
				need_vmlinux_btf = true;
			else
				need_kallsyms = true;
		} else {
			pr_warn("unrecognized extern '%s'\n", ext->name);
			return -EINVAL;
		}
	}
	if (need_config && extra_kconfig) {
		err = bpf_object__read_kconfig_mem(obj, extra_kconfig, kcfg_data);
		if (err)
			return -EINVAL;
		need_config = false;
		for (i = 0; i < obj->nr_extern; i++) {
			ext = &obj->externs[i];
			if (ext->type == EXT_KCFG && !ext->is_set) {
				need_config = true;
				break;
			}
		}
	}
	if (need_config) {
		err = bpf_object__read_kconfig_file(obj, kcfg_data);
		if (err)
			return -EINVAL;
	}
	if (need_kallsyms) {
		err = bpf_object__read_kallsyms_file(obj);
		if (err)
			return -EINVAL;
	}
	if (need_vmlinux_btf) {
		err = bpf_object__resolve_ksyms_btf_id(obj);
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
		return libbpf_err(-EINVAL);
	obj = attr->obj;
	if (!obj)
		return libbpf_err(-EINVAL);

	if (obj->loaded) {
		pr_warn("object '%s': load can't be attempted twice\n", obj->name);
		return libbpf_err(-EINVAL);
	}

	if (obj->gen_loader)
		bpf_gen__init(obj->gen_loader, attr->log_level);

	err = bpf_object__probe_loading(obj);
	err = err ? : bpf_object__load_vmlinux_btf(obj, false);
	err = err ? : bpf_object__resolve_externs(obj, obj->kconfig);
	err = err ? : bpf_object__sanitize_and_load_btf(obj);
	err = err ? : bpf_object__sanitize_maps(obj);
	err = err ? : bpf_object__init_kern_struct_ops_maps(obj);
	err = err ? : bpf_object__create_maps(obj);
	err = err ? : bpf_object__relocate(obj, obj->btf_custom_path ? : attr->target_btf_path);
	err = err ? : bpf_object__load_progs(obj, attr->log_level);

	if (obj->gen_loader) {
		/* reset FDs */
		btf__set_fd(obj->btf, -1);
		for (i = 0; i < obj->nr_maps; i++)
			obj->maps[i].fd = -1;
		if (!err)
			err = bpf_gen__finish(obj->gen_loader);
	}

	/* clean up module BTFs */
	for (i = 0; i < obj->btf_module_cnt; i++) {
		close(obj->btf_modules[i].fd);
		btf__free(obj->btf_modules[i].btf);
		free(obj->btf_modules[i].name);
	}
	free(obj->btf_modules);

	/* clean up vmlinux BTF */
	btf__free(obj->btf_vmlinux);
	obj->btf_vmlinux = NULL;

	obj->loaded = true; /* doesn't matter if successfully or not */

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
	return libbpf_err(err);
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
		return libbpf_err(err);

	err = check_path(path);
	if (err)
		return libbpf_err(err);

	if (prog == NULL) {
		pr_warn("invalid program pointer\n");
		return libbpf_err(-EINVAL);
	}

	if (instance < 0 || instance >= prog->instances.nr) {
		pr_warn("invalid prog instance %d of prog %s (max %d)\n",
			instance, prog->name, prog->instances.nr);
		return libbpf_err(-EINVAL);
	}

	if (bpf_obj_pin(prog->instances.fds[instance], path)) {
		err = -errno;
		cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
		pr_warn("failed to pin program: %s\n", cp);
		return libbpf_err(err);
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
		return libbpf_err(err);

	if (prog == NULL) {
		pr_warn("invalid program pointer\n");
		return libbpf_err(-EINVAL);
	}

	if (instance < 0 || instance >= prog->instances.nr) {
		pr_warn("invalid prog instance %d of prog %s (max %d)\n",
			instance, prog->name, prog->instances.nr);
		return libbpf_err(-EINVAL);
	}

	err = unlink(path);
	if (err != 0)
		return libbpf_err(-errno);

	pr_debug("unpinned program '%s'\n", path);

	return 0;
}

int bpf_program__pin(struct bpf_program *prog, const char *path)
{
	int i, err;

	err = make_parent_dir(path);
	if (err)
		return libbpf_err(err);

	err = check_path(path);
	if (err)
		return libbpf_err(err);

	if (prog == NULL) {
		pr_warn("invalid program pointer\n");
		return libbpf_err(-EINVAL);
	}

	if (prog->instances.nr <= 0) {
		pr_warn("no instances of prog %s to pin\n", prog->name);
		return libbpf_err(-EINVAL);
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

	return libbpf_err(err);
}

int bpf_program__unpin(struct bpf_program *prog, const char *path)
{
	int i, err;

	err = check_path(path);
	if (err)
		return libbpf_err(err);

	if (prog == NULL) {
		pr_warn("invalid program pointer\n");
		return libbpf_err(-EINVAL);
	}

	if (prog->instances.nr <= 0) {
		pr_warn("no instances of prog %s to pin\n", prog->name);
		return libbpf_err(-EINVAL);
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
			return libbpf_err(-EINVAL);
		else if (len >= PATH_MAX)
			return libbpf_err(-ENAMETOOLONG);

		err = bpf_program__unpin_instance(prog, buf, i);
		if (err)
			return err;
	}

	err = rmdir(path);
	if (err)
		return libbpf_err(-errno);

	return 0;
}

int bpf_map__pin(struct bpf_map *map, const char *path)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err;

	if (map == NULL) {
		pr_warn("invalid map pointer\n");
		return libbpf_err(-EINVAL);
	}

	if (map->pin_path) {
		if (path && strcmp(path, map->pin_path)) {
			pr_warn("map '%s' already has pin path '%s' different from '%s'\n",
				bpf_map__name(map), map->pin_path, path);
			return libbpf_err(-EINVAL);
		} else if (map->pinned) {
			pr_debug("map '%s' already pinned at '%s'; not re-pinning\n",
				 bpf_map__name(map), map->pin_path);
			return 0;
		}
	} else {
		if (!path) {
			pr_warn("missing a path to pin map '%s' at\n",
				bpf_map__name(map));
			return libbpf_err(-EINVAL);
		} else if (map->pinned) {
			pr_warn("map '%s' already pinned\n", bpf_map__name(map));
			return libbpf_err(-EEXIST);
		}

		map->pin_path = strdup(path);
		if (!map->pin_path) {
			err = -errno;
			goto out_err;
		}
	}

	err = make_parent_dir(map->pin_path);
	if (err)
		return libbpf_err(err);

	err = check_path(map->pin_path);
	if (err)
		return libbpf_err(err);

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
	return libbpf_err(err);
}

int bpf_map__unpin(struct bpf_map *map, const char *path)
{
	int err;

	if (map == NULL) {
		pr_warn("invalid map pointer\n");
		return libbpf_err(-EINVAL);
	}

	if (map->pin_path) {
		if (path && strcmp(path, map->pin_path)) {
			pr_warn("map '%s' already has pin path '%s' different from '%s'\n",
				bpf_map__name(map), map->pin_path, path);
			return libbpf_err(-EINVAL);
		}
		path = map->pin_path;
	} else if (!path) {
		pr_warn("no path to unpin map '%s' from\n",
			bpf_map__name(map));
		return libbpf_err(-EINVAL);
	}

	err = check_path(path);
	if (err)
		return libbpf_err(err);

	err = unlink(path);
	if (err != 0)
		return libbpf_err(-errno);

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
			return libbpf_err(-errno);
	}

	free(map->pin_path);
	map->pin_path = new;
	return 0;
}

const char *bpf_map__get_pin_path(const struct bpf_map *map)
{
	return map->pin_path;
}

const char *bpf_map__pin_path(const struct bpf_map *map)
{
	return map->pin_path;
}

bool bpf_map__is_pinned(const struct bpf_map *map)
{
	return map->pinned;
}

static void sanitize_pin_path(char *s)
{
	/* bpffs disallows periods in path names */
	while (*s) {
		if (*s == '.')
			*s = '_';
		s++;
	}
}

int bpf_object__pin_maps(struct bpf_object *obj, const char *path)
{
	struct bpf_map *map;
	int err;

	if (!obj)
		return libbpf_err(-ENOENT);

	if (!obj->loaded) {
		pr_warn("object not yet loaded; load it first\n");
		return libbpf_err(-ENOENT);
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
			sanitize_pin_path(buf);
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

	return libbpf_err(err);
}

int bpf_object__unpin_maps(struct bpf_object *obj, const char *path)
{
	struct bpf_map *map;
	int err;

	if (!obj)
		return libbpf_err(-ENOENT);

	bpf_object__for_each_map(map, obj) {
		char *pin_path = NULL;
		char buf[PATH_MAX];

		if (path) {
			int len;

			len = snprintf(buf, PATH_MAX, "%s/%s", path,
				       bpf_map__name(map));
			if (len < 0)
				return libbpf_err(-EINVAL);
			else if (len >= PATH_MAX)
				return libbpf_err(-ENAMETOOLONG);
			sanitize_pin_path(buf);
			pin_path = buf;
		} else if (!map->pin_path) {
			continue;
		}

		err = bpf_map__unpin(map, pin_path);
		if (err)
			return libbpf_err(err);
	}

	return 0;
}

int bpf_object__pin_programs(struct bpf_object *obj, const char *path)
{
	struct bpf_program *prog;
	int err;

	if (!obj)
		return libbpf_err(-ENOENT);

	if (!obj->loaded) {
		pr_warn("object not yet loaded; load it first\n");
		return libbpf_err(-ENOENT);
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

	return libbpf_err(err);
}

int bpf_object__unpin_programs(struct bpf_object *obj, const char *path)
{
	struct bpf_program *prog;
	int err;

	if (!obj)
		return libbpf_err(-ENOENT);

	bpf_object__for_each_program(prog, obj) {
		char buf[PATH_MAX];
		int len;

		len = snprintf(buf, PATH_MAX, "%s/%s", path,
			       prog->pin_name);
		if (len < 0)
			return libbpf_err(-EINVAL);
		else if (len >= PATH_MAX)
			return libbpf_err(-ENAMETOOLONG);

		err = bpf_program__unpin(prog, buf);
		if (err)
			return libbpf_err(err);
	}

	return 0;
}

int bpf_object__pin(struct bpf_object *obj, const char *path)
{
	int err;

	err = bpf_object__pin_maps(obj, path);
	if (err)
		return libbpf_err(err);

	err = bpf_object__pin_programs(obj, path);
	if (err) {
		bpf_object__unpin_maps(obj, path);
		return libbpf_err(err);
	}

	return 0;
}

static void bpf_map__destroy(struct bpf_map *map)
{
	if (map->clear_priv)
		map->clear_priv(map, map->priv);
	map->priv = NULL;
	map->clear_priv = NULL;

	if (map->inner_map) {
		bpf_map__destroy(map->inner_map);
		zfree(&map->inner_map);
	}

	zfree(&map->init_slots);
	map->init_slots_sz = 0;

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

	if (map->fd >= 0)
		zclose(map->fd);
}

void bpf_object__close(struct bpf_object *obj)
{
	size_t i;

	if (IS_ERR_OR_NULL(obj))
		return;

	if (obj->clear_priv)
		obj->clear_priv(obj, obj->priv);

	bpf_gen__free(obj->gen_loader);
	bpf_object__elf_finish(obj);
	bpf_object__unload(obj);
	btf__free(obj->btf);
	btf_ext__free(obj->btf_ext);

	for (i = 0; i < obj->nr_maps; i++)
		bpf_map__destroy(&obj->maps[i]);

	zfree(&obj->btf_custom_path);
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
	return obj ? obj->name : libbpf_err_ptr(-EINVAL);
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

int bpf_object__set_kversion(struct bpf_object *obj, __u32 kern_version)
{
	if (obj->loaded)
		return libbpf_err(-EINVAL);

	obj->kern_version = kern_version;

	return 0;
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
	return obj ? obj->priv : libbpf_err_ptr(-EINVAL);
}

int bpf_object__gen_loader(struct bpf_object *obj, struct gen_loader_opts *opts)
{
	struct bpf_gen *gen;

	if (!opts)
		return -EFAULT;
	if (!OPTS_VALID(opts, gen_loader_opts))
		return -EINVAL;
	gen = calloc(sizeof(*gen), 1);
	if (!gen)
		return -ENOMEM;
	gen->opts = opts;
	obj->gen_loader = gen;
	return 0;
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
		return errno = EINVAL, NULL;
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
	} while (prog && prog_is_subprog(obj, prog));

	return prog;
}

struct bpf_program *
bpf_program__prev(struct bpf_program *next, const struct bpf_object *obj)
{
	struct bpf_program *prog = next;

	do {
		prog = __bpf_program__iter(prog, obj, false);
	} while (prog && prog_is_subprog(obj, prog));

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
	return prog ? prog->priv : libbpf_err_ptr(-EINVAL);
}

void bpf_program__set_ifindex(struct bpf_program *prog, __u32 ifindex)
{
	prog->prog_ifindex = ifindex;
}

const char *bpf_program__name(const struct bpf_program *prog)
{
	return prog->name;
}

const char *bpf_program__section_name(const struct bpf_program *prog)
{
	return prog->sec_name;
}

const char *bpf_program__title(const struct bpf_program *prog, bool needs_copy)
{
	const char *title;

	title = prog->sec_name;
	if (needs_copy) {
		title = strdup(title);
		if (!title) {
			pr_warn("failed to strdup program title\n");
			return libbpf_err_ptr(-ENOMEM);
		}
	}

	return title;
}

bool bpf_program__autoload(const struct bpf_program *prog)
{
	return prog->load;
}

int bpf_program__set_autoload(struct bpf_program *prog, bool autoload)
{
	if (prog->obj->loaded)
		return libbpf_err(-EINVAL);

	prog->load = autoload;
	return 0;
}

int bpf_program__fd(const struct bpf_program *prog)
{
	return bpf_program__nth_fd(prog, 0);
}

size_t bpf_program__size(const struct bpf_program *prog)
{
	return prog->insns_cnt * BPF_INSN_SZ;
}

int bpf_program__set_prep(struct bpf_program *prog, int nr_instances,
			  bpf_program_prep_t prep)
{
	int *instances_fds;

	if (nr_instances <= 0 || !prep)
		return libbpf_err(-EINVAL);

	if (prog->instances.nr > 0 || prog->instances.fds) {
		pr_warn("Can't set pre-processor after loading\n");
		return libbpf_err(-EINVAL);
	}

	instances_fds = malloc(sizeof(int) * nr_instances);
	if (!instances_fds) {
		pr_warn("alloc memory failed for fds\n");
		return libbpf_err(-ENOMEM);
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
		return libbpf_err(-EINVAL);

	if (n >= prog->instances.nr || n < 0) {
		pr_warn("Can't get the %dth fd from program %s: only %d instances\n",
			n, prog->name, prog->instances.nr);
		return libbpf_err(-EINVAL);
	}

	fd = prog->instances.fds[n];
	if (fd < 0) {
		pr_warn("%dth instance of program '%s' is invalid\n",
			n, prog->name);
		return libbpf_err(-ENOENT);
	}

	return fd;
}

enum bpf_prog_type bpf_program__get_type(const struct bpf_program *prog)
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
		return libbpf_err(-EINVAL);			\
	bpf_program__set_type(prog, TYPE);			\
	return 0;						\
}								\
								\
bool bpf_program__is_##NAME(const struct bpf_program *prog)	\
{								\
	return bpf_program__is_type(prog, TYPE);		\
}								\

BPF_PROG_TYPE_FNS(socket_filter, BPF_PROG_TYPE_SOCKET_FILTER);
BPF_PROG_TYPE_FNS(lsm, BPF_PROG_TYPE_LSM);
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
BPF_PROG_TYPE_FNS(sk_lookup, BPF_PROG_TYPE_SK_LOOKUP);

enum bpf_attach_type
bpf_program__get_expected_attach_type(const struct bpf_program *prog)
{
	return prog->expected_attach_type;
}

void bpf_program__set_expected_attach_type(struct bpf_program *prog,
					   enum bpf_attach_type type)
{
	prog->expected_attach_type = type;
}

#define BPF_PROG_SEC_IMPL(string, ptype, eatype, eatype_optional,	    \
			  attachable, attach_btf)			    \
	{								    \
		.sec = string,						    \
		.len = sizeof(string) - 1,				    \
		.prog_type = ptype,					    \
		.expected_attach_type = eatype,				    \
		.is_exp_attach_type_optional = eatype_optional,		    \
		.is_attachable = attachable,				    \
		.is_attach_btf = attach_btf,				    \
	}

/* Programs that can NOT be attached. */
#define BPF_PROG_SEC(string, ptype) BPF_PROG_SEC_IMPL(string, ptype, 0, 0, 0, 0)

/* Programs that can be attached. */
#define BPF_APROG_SEC(string, ptype, atype) \
	BPF_PROG_SEC_IMPL(string, ptype, atype, true, 1, 0)

/* Programs that must specify expected attach type at load time. */
#define BPF_EAPROG_SEC(string, ptype, eatype) \
	BPF_PROG_SEC_IMPL(string, ptype, eatype, false, 1, 0)

/* Programs that use BTF to identify attach point */
#define BPF_PROG_BTF(string, ptype, eatype) \
	BPF_PROG_SEC_IMPL(string, ptype, eatype, false, 0, 1)

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

static struct bpf_link *attach_kprobe(const struct bpf_sec_def *sec,
				      struct bpf_program *prog);
static struct bpf_link *attach_tp(const struct bpf_sec_def *sec,
				  struct bpf_program *prog);
static struct bpf_link *attach_raw_tp(const struct bpf_sec_def *sec,
				      struct bpf_program *prog);
static struct bpf_link *attach_trace(const struct bpf_sec_def *sec,
				     struct bpf_program *prog);
static struct bpf_link *attach_lsm(const struct bpf_sec_def *sec,
				   struct bpf_program *prog);
static struct bpf_link *attach_iter(const struct bpf_sec_def *sec,
				    struct bpf_program *prog);

static const struct bpf_sec_def section_defs[] = {
	BPF_PROG_SEC("socket",			BPF_PROG_TYPE_SOCKET_FILTER),
	BPF_EAPROG_SEC("sk_reuseport/migrate",	BPF_PROG_TYPE_SK_REUSEPORT,
						BPF_SK_REUSEPORT_SELECT_OR_MIGRATE),
	BPF_EAPROG_SEC("sk_reuseport",		BPF_PROG_TYPE_SK_REUSEPORT,
						BPF_SK_REUSEPORT_SELECT),
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
	SEC_DEF("fmod_ret/", TRACING,
		.expected_attach_type = BPF_MODIFY_RETURN,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
	SEC_DEF("fexit/", TRACING,
		.expected_attach_type = BPF_TRACE_FEXIT,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
	SEC_DEF("fentry.s/", TRACING,
		.expected_attach_type = BPF_TRACE_FENTRY,
		.is_attach_btf = true,
		.is_sleepable = true,
		.attach_fn = attach_trace),
	SEC_DEF("fmod_ret.s/", TRACING,
		.expected_attach_type = BPF_MODIFY_RETURN,
		.is_attach_btf = true,
		.is_sleepable = true,
		.attach_fn = attach_trace),
	SEC_DEF("fexit.s/", TRACING,
		.expected_attach_type = BPF_TRACE_FEXIT,
		.is_attach_btf = true,
		.is_sleepable = true,
		.attach_fn = attach_trace),
	SEC_DEF("freplace/", EXT,
		.is_attach_btf = true,
		.attach_fn = attach_trace),
	SEC_DEF("lsm/", LSM,
		.is_attach_btf = true,
		.expected_attach_type = BPF_LSM_MAC,
		.attach_fn = attach_lsm),
	SEC_DEF("lsm.s/", LSM,
		.is_attach_btf = true,
		.is_sleepable = true,
		.expected_attach_type = BPF_LSM_MAC,
		.attach_fn = attach_lsm),
	SEC_DEF("iter/", TRACING,
		.expected_attach_type = BPF_TRACE_ITER,
		.is_attach_btf = true,
		.attach_fn = attach_iter),
	SEC_DEF("syscall", SYSCALL,
		.is_sleepable = true),
	BPF_EAPROG_SEC("xdp_devmap/",		BPF_PROG_TYPE_XDP,
						BPF_XDP_DEVMAP),
	BPF_EAPROG_SEC("xdp_cpumap/",		BPF_PROG_TYPE_XDP,
						BPF_XDP_CPUMAP),
	BPF_APROG_SEC("xdp",			BPF_PROG_TYPE_XDP,
						BPF_XDP),
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
	BPF_EAPROG_SEC("cgroup/sock_create",	BPF_PROG_TYPE_CGROUP_SOCK,
						BPF_CGROUP_INET_SOCK_CREATE),
	BPF_EAPROG_SEC("cgroup/sock_release",	BPF_PROG_TYPE_CGROUP_SOCK,
						BPF_CGROUP_INET_SOCK_RELEASE),
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
	BPF_EAPROG_SEC("cgroup/getpeername4",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET4_GETPEERNAME),
	BPF_EAPROG_SEC("cgroup/getpeername6",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET6_GETPEERNAME),
	BPF_EAPROG_SEC("cgroup/getsockname4",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET4_GETSOCKNAME),
	BPF_EAPROG_SEC("cgroup/getsockname6",	BPF_PROG_TYPE_CGROUP_SOCK_ADDR,
						BPF_CGROUP_INET6_GETSOCKNAME),
	BPF_EAPROG_SEC("cgroup/sysctl",		BPF_PROG_TYPE_CGROUP_SYSCTL,
						BPF_CGROUP_SYSCTL),
	BPF_EAPROG_SEC("cgroup/getsockopt",	BPF_PROG_TYPE_CGROUP_SOCKOPT,
						BPF_CGROUP_GETSOCKOPT),
	BPF_EAPROG_SEC("cgroup/setsockopt",	BPF_PROG_TYPE_CGROUP_SOCKOPT,
						BPF_CGROUP_SETSOCKOPT),
	BPF_PROG_SEC("struct_ops",		BPF_PROG_TYPE_STRUCT_OPS),
	BPF_EAPROG_SEC("sk_lookup/",		BPF_PROG_TYPE_SK_LOOKUP,
						BPF_SK_LOOKUP),
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
		return libbpf_err(-EINVAL);

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

	return libbpf_err(-ESRCH);
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
static int bpf_object__collect_st_ops_relos(struct bpf_object *obj,
					    GElf_Shdr *shdr, Elf_Data *data)
{
	const struct btf_member *member;
	struct bpf_struct_ops *st_ops;
	struct bpf_program *prog;
	unsigned int shdr_idx;
	const struct btf *btf;
	struct bpf_map *map;
	Elf_Data *symbols;
	unsigned int moff, insn_idx;
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

		name = elf_sym_str(obj, sym.st_name) ?: "<?>";
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
		if (sym.st_value % BPF_INSN_SZ) {
			pr_warn("struct_ops reloc %s: invalid target program offset %llu\n",
				map->name, (unsigned long long)sym.st_value);
			return -LIBBPF_ERRNO__FORMAT;
		}
		insn_idx = sym.st_value / BPF_INSN_SZ;

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

		prog = find_prog_by_sec_insn(obj, shdr_idx, insn_idx);
		if (!prog) {
			pr_warn("struct_ops reloc %s: cannot find prog at shdr_idx %u to relocate func ptr %s\n",
				map->name, shdr_idx, name);
			return -EINVAL;
		}

		/* prevent the use of BPF prog with invalid type */
		if (prog->type != BPF_PROG_TYPE_STRUCT_OPS) {
			pr_warn("struct_ops reloc %s: prog %s is not struct_ops BPF program\n",
				map->name, prog->name);
			return -EINVAL;
		}

		/* if we haven't yet processed this BPF program, record proper
		 * attach_btf_id and member_idx
		 */
		if (!prog->attach_btf_id) {
			prog->attach_btf_id = st_ops->type_id;
			prog->expected_attach_type = member_idx;
		}

		/* struct_ops BPF prog can be re-used between multiple
		 * .struct_ops as long as it's the same struct_ops struct
		 * definition and the same function pointer field
		 */
		if (prog->attach_btf_id != st_ops->type_id ||
		    prog->expected_attach_type != member_idx) {
			pr_warn("struct_ops reloc %s: cannot use prog %s in sec %s with type %u attach_btf_id %u expected_attach_type %u for func ptr %s\n",
				map->name, prog->name, prog->sec_name, prog->type,
				prog->attach_btf_id, prog->expected_attach_type, name);
			return -EINVAL;
		}

		st_ops->progs[member_idx] = prog;
	}

	return 0;
}

#define BTF_TRACE_PREFIX "btf_trace_"
#define BTF_LSM_PREFIX "bpf_lsm_"
#define BTF_ITER_PREFIX "bpf_iter_"
#define BTF_MAX_NAME_SIZE 128

void btf_get_kernel_prefix_kind(enum bpf_attach_type attach_type,
				const char **prefix, int *kind)
{
	switch (attach_type) {
	case BPF_TRACE_RAW_TP:
		*prefix = BTF_TRACE_PREFIX;
		*kind = BTF_KIND_TYPEDEF;
		break;
	case BPF_LSM_MAC:
		*prefix = BTF_LSM_PREFIX;
		*kind = BTF_KIND_FUNC;
		break;
	case BPF_TRACE_ITER:
		*prefix = BTF_ITER_PREFIX;
		*kind = BTF_KIND_FUNC;
		break;
	default:
		*prefix = "";
		*kind = BTF_KIND_FUNC;
	}
}

static int find_btf_by_prefix_kind(const struct btf *btf, const char *prefix,
				   const char *name, __u32 kind)
{
	char btf_type_name[BTF_MAX_NAME_SIZE];
	int ret;

	ret = snprintf(btf_type_name, sizeof(btf_type_name),
		       "%s%s", prefix, name);
	/* snprintf returns the number of characters written excluding the
	 * terminating null. So, if >= BTF_MAX_NAME_SIZE are written, it
	 * indicates truncation.
	 */
	if (ret < 0 || ret >= sizeof(btf_type_name))
		return -ENAMETOOLONG;
	return btf__find_by_name_kind(btf, btf_type_name, kind);
}

static inline int find_attach_btf_id(struct btf *btf, const char *name,
				     enum bpf_attach_type attach_type)
{
	const char *prefix;
	int kind;

	btf_get_kernel_prefix_kind(attach_type, &prefix, &kind);
	return find_btf_by_prefix_kind(btf, prefix, name, kind);
}

int libbpf_find_vmlinux_btf_id(const char *name,
			       enum bpf_attach_type attach_type)
{
	struct btf *btf;
	int err;

	btf = btf__load_vmlinux_btf();
	err = libbpf_get_error(btf);
	if (err) {
		pr_warn("vmlinux BTF is not found\n");
		return libbpf_err(err);
	}

	err = find_attach_btf_id(btf, name, attach_type);
	if (err <= 0)
		pr_warn("%s is not found in vmlinux BTF\n", name);

	btf__free(btf);
	return libbpf_err(err);
}

static int libbpf_find_prog_btf_id(const char *name, __u32 attach_prog_fd)
{
	struct bpf_prog_info_linear *info_linear;
	struct bpf_prog_info *info;
	struct btf *btf;
	int err;

	info_linear = bpf_program__get_prog_info_linear(attach_prog_fd, 0);
	err = libbpf_get_error(info_linear);
	if (err) {
		pr_warn("failed get_prog_info_linear for FD %d\n",
			attach_prog_fd);
		return err;
	}

	err = -EINVAL;
	info = &info_linear->info;
	if (!info->btf_id) {
		pr_warn("The target program doesn't have BTF\n");
		goto out;
	}
	btf = btf__load_from_kernel_by_id(info->btf_id);
	if (libbpf_get_error(btf)) {
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

static int find_kernel_btf_id(struct bpf_object *obj, const char *attach_name,
			      enum bpf_attach_type attach_type,
			      int *btf_obj_fd, int *btf_type_id)
{
	int ret, i;

	ret = find_attach_btf_id(obj->btf_vmlinux, attach_name, attach_type);
	if (ret > 0) {
		*btf_obj_fd = 0; /* vmlinux BTF */
		*btf_type_id = ret;
		return 0;
	}
	if (ret != -ENOENT)
		return ret;

	ret = load_module_btfs(obj);
	if (ret)
		return ret;

	for (i = 0; i < obj->btf_module_cnt; i++) {
		const struct module_btf *mod = &obj->btf_modules[i];

		ret = find_attach_btf_id(mod->btf, attach_name, attach_type);
		if (ret > 0) {
			*btf_obj_fd = mod->fd;
			*btf_type_id = ret;
			return 0;
		}
		if (ret == -ENOENT)
			continue;

		return ret;
	}

	return -ESRCH;
}

static int libbpf_find_attach_btf_id(struct bpf_program *prog, int *btf_obj_fd, int *btf_type_id)
{
	enum bpf_attach_type attach_type = prog->expected_attach_type;
	__u32 attach_prog_fd = prog->attach_prog_fd;
	const char *name = prog->sec_name, *attach_name;
	const struct bpf_sec_def *sec = NULL;
	int i, err = 0;

	if (!name)
		return -EINVAL;

	for (i = 0; i < ARRAY_SIZE(section_defs); i++) {
		if (!section_defs[i].is_attach_btf)
			continue;
		if (strncmp(name, section_defs[i].sec, section_defs[i].len))
			continue;

		sec = &section_defs[i];
		break;
	}

	if (!sec) {
		pr_warn("failed to identify BTF ID based on ELF section name '%s'\n", name);
		return -ESRCH;
	}
	attach_name = name + sec->len;

	/* BPF program's BTF ID */
	if (attach_prog_fd) {
		err = libbpf_find_prog_btf_id(attach_name, attach_prog_fd);
		if (err < 0) {
			pr_warn("failed to find BPF program (FD %d) BTF ID for '%s': %d\n",
				 attach_prog_fd, attach_name, err);
			return err;
		}
		*btf_obj_fd = 0;
		*btf_type_id = err;
		return 0;
	}

	/* kernel/module BTF ID */
	if (prog->obj->gen_loader) {
		bpf_gen__record_attach_target(prog->obj->gen_loader, attach_name, attach_type);
		*btf_obj_fd = 0;
		*btf_type_id = 1;
	} else {
		err = find_kernel_btf_id(prog->obj, attach_name, attach_type, btf_obj_fd, btf_type_id);
	}
	if (err) {
		pr_warn("failed to find kernel BTF type ID of '%s': %d\n", attach_name, err);
		return err;
	}
	return 0;
}

int libbpf_attach_type_by_name(const char *name,
			       enum bpf_attach_type *attach_type)
{
	char *type_names;
	int i;

	if (!name)
		return libbpf_err(-EINVAL);

	for (i = 0; i < ARRAY_SIZE(section_defs); i++) {
		if (strncmp(name, section_defs[i].sec, section_defs[i].len))
			continue;
		if (!section_defs[i].is_attachable)
			return libbpf_err(-EINVAL);
		*attach_type = section_defs[i].expected_attach_type;
		return 0;
	}
	pr_debug("failed to guess attach type based on ELF section name '%s'\n", name);
	type_names = libbpf_get_type_names(true);
	if (type_names != NULL) {
		pr_debug("attachable section(type) names are:%s\n", type_names);
		free(type_names);
	}

	return libbpf_err(-EINVAL);
}

int bpf_map__fd(const struct bpf_map *map)
{
	return map ? map->fd : libbpf_err(-EINVAL);
}

const struct bpf_map_def *bpf_map__def(const struct bpf_map *map)
{
	return map ? &map->def : libbpf_err_ptr(-EINVAL);
}

const char *bpf_map__name(const struct bpf_map *map)
{
	return map ? map->name : NULL;
}

enum bpf_map_type bpf_map__type(const struct bpf_map *map)
{
	return map->def.type;
}

int bpf_map__set_type(struct bpf_map *map, enum bpf_map_type type)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->def.type = type;
	return 0;
}

__u32 bpf_map__map_flags(const struct bpf_map *map)
{
	return map->def.map_flags;
}

int bpf_map__set_map_flags(struct bpf_map *map, __u32 flags)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->def.map_flags = flags;
	return 0;
}

__u32 bpf_map__numa_node(const struct bpf_map *map)
{
	return map->numa_node;
}

int bpf_map__set_numa_node(struct bpf_map *map, __u32 numa_node)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->numa_node = numa_node;
	return 0;
}

__u32 bpf_map__key_size(const struct bpf_map *map)
{
	return map->def.key_size;
}

int bpf_map__set_key_size(struct bpf_map *map, __u32 size)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->def.key_size = size;
	return 0;
}

__u32 bpf_map__value_size(const struct bpf_map *map)
{
	return map->def.value_size;
}

int bpf_map__set_value_size(struct bpf_map *map, __u32 size)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->def.value_size = size;
	return 0;
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
		return libbpf_err(-EINVAL);

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
	return map ? map->priv : libbpf_err_ptr(-EINVAL);
}

int bpf_map__set_initial_value(struct bpf_map *map,
			       const void *data, size_t size)
{
	if (!map->mmaped || map->libbpf_type == LIBBPF_MAP_KCONFIG ||
	    size != map->def.value_size || map->fd >= 0)
		return libbpf_err(-EINVAL);

	memcpy(map->mmaped, data, size);
	return 0;
}

const void *bpf_map__initial_value(struct bpf_map *map, size_t *psize)
{
	if (!map->mmaped)
		return NULL;
	*psize = map->def.value_size;
	return map->mmaped;
}

bool bpf_map__is_offload_neutral(const struct bpf_map *map)
{
	return map->def.type == BPF_MAP_TYPE_PERF_EVENT_ARRAY;
}

bool bpf_map__is_internal(const struct bpf_map *map)
{
	return map->libbpf_type != LIBBPF_MAP_UNSPEC;
}

__u32 bpf_map__ifindex(const struct bpf_map *map)
{
	return map->map_ifindex;
}

int bpf_map__set_ifindex(struct bpf_map *map, __u32 ifindex)
{
	if (map->fd >= 0)
		return libbpf_err(-EBUSY);
	map->map_ifindex = ifindex;
	return 0;
}

int bpf_map__set_inner_map_fd(struct bpf_map *map, int fd)
{
	if (!bpf_map_type__is_map_in_map(map->def.type)) {
		pr_warn("error: unsupported map type\n");
		return libbpf_err(-EINVAL);
	}
	if (map->inner_map_fd != -1) {
		pr_warn("error: inner_map_fd already specified\n");
		return libbpf_err(-EINVAL);
	}
	zfree(&map->inner_map);
	map->inner_map_fd = fd;
	return 0;
}

static struct bpf_map *
__bpf_map__iter(const struct bpf_map *m, const struct bpf_object *obj, int i)
{
	ssize_t idx;
	struct bpf_map *s, *e;

	if (!obj || !obj->maps)
		return errno = EINVAL, NULL;

	s = obj->maps;
	e = obj->maps + obj->nr_maps;

	if ((m < s) || (m >= e)) {
		pr_warn("error in %s: map handler doesn't belong to object\n",
			 __func__);
		return errno = EINVAL, NULL;
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
	return errno = ENOENT, NULL;
}

int
bpf_object__find_map_fd_by_name(const struct bpf_object *obj, const char *name)
{
	return bpf_map__fd(bpf_object__find_map_by_name(obj, name));
}

struct bpf_map *
bpf_object__find_map_by_offset(struct bpf_object *obj, size_t offset)
{
	return libbpf_err_ptr(-ENOTSUP);
}

long libbpf_get_error(const void *ptr)
{
	if (!IS_ERR_OR_NULL(ptr))
		return 0;

	if (IS_ERR(ptr))
		errno = -PTR_ERR(ptr);

	/* If ptr == NULL, then errno should be already set by the failing
	 * API, because libbpf never returns NULL on success and it now always
	 * sets errno on error. So no extra errno handling for ptr == NULL
	 * case.
	 */
	return -errno;
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
		return libbpf_err(-EINVAL);
	if (!attr->file)
		return libbpf_err(-EINVAL);

	open_attr.file = attr->file;
	open_attr.prog_type = attr->prog_type;

	obj = bpf_object__open_xattr(&open_attr);
	err = libbpf_get_error(obj);
	if (err)
		return libbpf_err(-ENOENT);

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
			return libbpf_err(-EINVAL);
		}

		prog->prog_ifindex = attr->ifindex;
		prog->log_level = attr->log_level;
		prog->prog_flags |= attr->prog_flags;
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
		return libbpf_err(-ENOENT);
	}

	err = bpf_object__load(obj);
	if (err) {
		bpf_object__close(obj);
		return libbpf_err(err);
	}

	*pobj = obj;
	*prog_fd = bpf_program__fd(first_prog);
	return 0;
}

struct bpf_link {
	int (*detach)(struct bpf_link *link);
	void (*dealloc)(struct bpf_link *link);
	char *pin_path;		/* NULL, if not pinned */
	int fd;			/* hook FD, -1 if not applicable */
	bool disconnected;
};

/* Replace link's underlying BPF program with the new one */
int bpf_link__update_program(struct bpf_link *link, struct bpf_program *prog)
{
	int ret;

	ret = bpf_link_update(bpf_link__fd(link), bpf_program__fd(prog), NULL);
	return libbpf_err_errno(ret);
}

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

	if (IS_ERR_OR_NULL(link))
		return 0;

	if (!link->disconnected && link->detach)
		err = link->detach(link);
	if (link->pin_path)
		free(link->pin_path);
	if (link->dealloc)
		link->dealloc(link);
	else
		free(link);

	return libbpf_err(err);
}

int bpf_link__fd(const struct bpf_link *link)
{
	return link->fd;
}

const char *bpf_link__pin_path(const struct bpf_link *link)
{
	return link->pin_path;
}

static int bpf_link__detach_fd(struct bpf_link *link)
{
	return libbpf_err_errno(close(link->fd));
}

struct bpf_link *bpf_link__open(const char *path)
{
	struct bpf_link *link;
	int fd;

	fd = bpf_obj_get(path);
	if (fd < 0) {
		fd = -errno;
		pr_warn("failed to open link at %s: %d\n", path, fd);
		return libbpf_err_ptr(fd);
	}

	link = calloc(1, sizeof(*link));
	if (!link) {
		close(fd);
		return libbpf_err_ptr(-ENOMEM);
	}
	link->detach = &bpf_link__detach_fd;
	link->fd = fd;

	link->pin_path = strdup(path);
	if (!link->pin_path) {
		bpf_link__destroy(link);
		return libbpf_err_ptr(-ENOMEM);
	}

	return link;
}

int bpf_link__detach(struct bpf_link *link)
{
	return bpf_link_detach(link->fd) ? -errno : 0;
}

int bpf_link__pin(struct bpf_link *link, const char *path)
{
	int err;

	if (link->pin_path)
		return libbpf_err(-EBUSY);
	err = make_parent_dir(path);
	if (err)
		return libbpf_err(err);
	err = check_path(path);
	if (err)
		return libbpf_err(err);

	link->pin_path = strdup(path);
	if (!link->pin_path)
		return libbpf_err(-ENOMEM);

	if (bpf_obj_pin(link->fd, link->pin_path)) {
		err = -errno;
		zfree(&link->pin_path);
		return libbpf_err(err);
	}

	pr_debug("link fd=%d: pinned at %s\n", link->fd, link->pin_path);
	return 0;
}

int bpf_link__unpin(struct bpf_link *link)
{
	int err;

	if (!link->pin_path)
		return libbpf_err(-EINVAL);

	err = unlink(link->pin_path);
	if (err != 0)
		return -errno;

	pr_debug("link fd=%d: unpinned from %s\n", link->fd, link->pin_path);
	zfree(&link->pin_path);
	return 0;
}

static int poke_kprobe_events(bool add, const char *name, bool retprobe, uint64_t offset)
{
	int fd, ret = 0;
	pid_t p = getpid();
	char cmd[260], probename[128], probefunc[128];
	const char *file = "/sys/kernel/debug/tracing/kprobe_events";

	if (retprobe)
		snprintf(probename, sizeof(probename), "kretprobes/%s_libbpf_%u", name, p);
	else
		snprintf(probename, sizeof(probename), "kprobes/%s_libbpf_%u", name, p);

	if (offset)
		snprintf(probefunc, sizeof(probefunc), "%s+%zu", name, (size_t)offset);

	if (add) {
		snprintf(cmd, sizeof(cmd), "%c:%s %s",
			 retprobe ? 'r' : 'p',
			 probename,
			 offset ? probefunc : name);
	} else {
		snprintf(cmd, sizeof(cmd), "-:%s", probename);
	}

	fd = open(file, O_WRONLY | O_APPEND, 0);
	if (!fd)
		return -errno;
	ret = write(fd, cmd, strlen(cmd));
	if (ret < 0)
		ret = -errno;
	close(fd);

	return ret;
}

static inline int add_kprobe_event_legacy(const char *name, bool retprobe, uint64_t offset)
{
	return poke_kprobe_events(true, name, retprobe, offset);
}

static inline int remove_kprobe_event_legacy(const char *name, bool retprobe)
{
	return poke_kprobe_events(false, name, retprobe, 0);
}

struct bpf_link_perf {
	struct bpf_link link;
	int perf_event_fd;
	/* legacy kprobe support: keep track of probe identifier and type */
	char *legacy_probe_name;
	bool legacy_is_retprobe;
};

static int bpf_link_perf_detach(struct bpf_link *link)
{
	struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);
	int err = 0;

	if (ioctl(perf_link->perf_event_fd, PERF_EVENT_IOC_DISABLE, 0) < 0)
		err = -errno;

	if (perf_link->perf_event_fd != link->fd)
		close(perf_link->perf_event_fd);
	close(link->fd);

	/* legacy kprobe needs to be removed after perf event fd closure */
	if (perf_link->legacy_probe_name)
		err = remove_kprobe_event_legacy(perf_link->legacy_probe_name,
						 perf_link->legacy_is_retprobe);

	return err;
}

static void bpf_link_perf_dealloc(struct bpf_link *link)
{
	struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);

	free(perf_link->legacy_probe_name);
	free(perf_link);
}

struct bpf_link *bpf_program__attach_perf_event_opts(struct bpf_program *prog, int pfd,
						     const struct bpf_perf_event_opts *opts)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link_perf *link;
	int prog_fd, link_fd = -1, err;

	if (!OPTS_VALID(opts, bpf_perf_event_opts))
		return libbpf_err_ptr(-EINVAL);

	if (pfd < 0) {
		pr_warn("prog '%s': invalid perf event FD %d\n",
			prog->name, pfd);
		return libbpf_err_ptr(-EINVAL);
	}
	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach BPF program w/o FD (did you load it?)\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->link.detach = &bpf_link_perf_detach;
	link->link.dealloc = &bpf_link_perf_dealloc;
	link->perf_event_fd = pfd;

	if (kernel_supports(prog->obj, FEAT_PERF_LINK)) {
		DECLARE_LIBBPF_OPTS(bpf_link_create_opts, link_opts,
			.perf_event.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0));

		link_fd = bpf_link_create(prog_fd, pfd, BPF_PERF_EVENT, &link_opts);
		if (link_fd < 0) {
			err = -errno;
			pr_warn("prog '%s': failed to create BPF link for perf_event FD %d: %d (%s)\n",
				prog->name, pfd,
				err, libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
			goto err_out;
		}
		link->link.fd = link_fd;
	} else {
		if (OPTS_GET(opts, bpf_cookie, 0)) {
			pr_warn("prog '%s': user context value is not supported\n", prog->name);
			err = -EOPNOTSUPP;
			goto err_out;
		}

		if (ioctl(pfd, PERF_EVENT_IOC_SET_BPF, prog_fd) < 0) {
			err = -errno;
			pr_warn("prog '%s': failed to attach to perf_event FD %d: %s\n",
				prog->name, pfd, libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
			if (err == -EPROTO)
				pr_warn("prog '%s': try add PERF_SAMPLE_CALLCHAIN to or remove exclude_callchain_[kernel|user] from pfd %d\n",
					prog->name, pfd);
			goto err_out;
		}
		link->link.fd = pfd;
	}
	if (ioctl(pfd, PERF_EVENT_IOC_ENABLE, 0) < 0) {
		err = -errno;
		pr_warn("prog '%s': failed to enable perf_event FD %d: %s\n",
			prog->name, pfd, libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto err_out;
	}

	return &link->link;
err_out:
	if (link_fd >= 0)
		close(link_fd);
	free(link);
	return libbpf_err_ptr(err);
}

struct bpf_link *bpf_program__attach_perf_event(struct bpf_program *prog, int pfd)
{
	return bpf_program__attach_perf_event_opts(prog, pfd, NULL);
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

static int determine_kprobe_perf_type_legacy(const char *func_name, bool is_retprobe)
{
	char file[192];

	snprintf(file, sizeof(file),
		 "/sys/kernel/debug/tracing/events/%s/%s_libbpf_%d/id",
		 is_retprobe ? "kretprobes" : "kprobes",
		 func_name, getpid());

	return parse_uint_from_file(file, "%d\n");
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

#define PERF_UPROBE_REF_CTR_OFFSET_BITS 32
#define PERF_UPROBE_REF_CTR_OFFSET_SHIFT 32

static int perf_event_open_probe(bool uprobe, bool retprobe, const char *name,
				 uint64_t offset, int pid, size_t ref_ctr_off)
{
	struct perf_event_attr attr = {};
	char errmsg[STRERR_BUFSIZE];
	int type, pfd, err;

	if (ref_ctr_off >= (1ULL << PERF_UPROBE_REF_CTR_OFFSET_BITS))
		return -EINVAL;

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
	attr.config |= (__u64)ref_ctr_off << PERF_UPROBE_REF_CTR_OFFSET_SHIFT;
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

static int perf_event_kprobe_open_legacy(bool retprobe, const char *name, uint64_t offset, int pid)
{
	struct perf_event_attr attr = {};
	char errmsg[STRERR_BUFSIZE];
	int type, pfd, err;

	err = add_kprobe_event_legacy(name, retprobe, offset);
	if (err < 0) {
		pr_warn("failed to add legacy kprobe event: %s\n",
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return err;
	}
	type = determine_kprobe_perf_type_legacy(name, retprobe);
	if (type < 0) {
		pr_warn("failed to determine legacy kprobe event id: %s\n",
			libbpf_strerror_r(type, errmsg, sizeof(errmsg)));
		return type;
	}
	attr.size = sizeof(attr);
	attr.config = type;
	attr.type = PERF_TYPE_TRACEPOINT;

	pfd = syscall(__NR_perf_event_open, &attr,
		      pid < 0 ? -1 : pid, /* pid */
		      pid == -1 ? 0 : -1, /* cpu */
		      -1 /* group_fd */,  PERF_FLAG_FD_CLOEXEC);
	if (pfd < 0) {
		err = -errno;
		pr_warn("legacy kprobe perf_event_open() failed: %s\n",
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return err;
	}
	return pfd;
}

struct bpf_link *
bpf_program__attach_kprobe_opts(struct bpf_program *prog,
				const char *func_name,
				const struct bpf_kprobe_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, pe_opts);
	char errmsg[STRERR_BUFSIZE];
	char *legacy_probe = NULL;
	struct bpf_link *link;
	unsigned long offset;
	bool retprobe, legacy;
	int pfd, err;

	if (!OPTS_VALID(opts, bpf_kprobe_opts))
		return libbpf_err_ptr(-EINVAL);

	retprobe = OPTS_GET(opts, retprobe, false);
	offset = OPTS_GET(opts, offset, 0);
	pe_opts.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0);

	legacy = determine_kprobe_perf_type() < 0;
	if (!legacy) {
		pfd = perf_event_open_probe(false /* uprobe */, retprobe,
					    func_name, offset,
					    -1 /* pid */, 0 /* ref_ctr_off */);
	} else {
		legacy_probe = strdup(func_name);
		if (!legacy_probe)
			return libbpf_err_ptr(-ENOMEM);

		pfd = perf_event_kprobe_open_legacy(retprobe, func_name,
						    offset, -1 /* pid */);
	}
	if (pfd < 0) {
		pr_warn("prog '%s': failed to create %s '%s' perf event: %s\n",
			prog->name, retprobe ? "kretprobe" : "kprobe", func_name,
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(pfd);
	}
	link = bpf_program__attach_perf_event_opts(prog, pfd, &pe_opts);
	err = libbpf_get_error(link);
	if (err) {
		close(pfd);
		pr_warn("prog '%s': failed to attach to %s '%s': %s\n",
			prog->name, retprobe ? "kretprobe" : "kprobe", func_name,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(err);
	}
	if (legacy) {
		struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);

		perf_link->legacy_probe_name = legacy_probe;
		perf_link->legacy_is_retprobe = retprobe;
	}

	return link;
}

struct bpf_link *bpf_program__attach_kprobe(struct bpf_program *prog,
					    bool retprobe,
					    const char *func_name)
{
	DECLARE_LIBBPF_OPTS(bpf_kprobe_opts, opts,
		.retprobe = retprobe,
	);

	return bpf_program__attach_kprobe_opts(prog, func_name, &opts);
}

static struct bpf_link *attach_kprobe(const struct bpf_sec_def *sec,
				      struct bpf_program *prog)
{
	DECLARE_LIBBPF_OPTS(bpf_kprobe_opts, opts);
	unsigned long offset = 0;
	struct bpf_link *link;
	const char *func_name;
	char *func;
	int n, err;

	func_name = prog->sec_name + sec->len;
	opts.retprobe = strcmp(sec->sec, "kretprobe/") == 0;

	n = sscanf(func_name, "%m[a-zA-Z0-9_.]+%li", &func, &offset);
	if (n < 1) {
		err = -EINVAL;
		pr_warn("kprobe name is invalid: %s\n", func_name);
		return libbpf_err_ptr(err);
	}
	if (opts.retprobe && offset != 0) {
		free(func);
		err = -EINVAL;
		pr_warn("kretprobes do not support offset specification\n");
		return libbpf_err_ptr(err);
	}

	opts.offset = offset;
	link = bpf_program__attach_kprobe_opts(prog, func, &opts);
	free(func);
	return link;
}

LIBBPF_API struct bpf_link *
bpf_program__attach_uprobe_opts(struct bpf_program *prog, pid_t pid,
				const char *binary_path, size_t func_offset,
				const struct bpf_uprobe_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, pe_opts);
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	size_t ref_ctr_off;
	int pfd, err;
	bool retprobe;

	if (!OPTS_VALID(opts, bpf_uprobe_opts))
		return libbpf_err_ptr(-EINVAL);

	retprobe = OPTS_GET(opts, retprobe, false);
	ref_ctr_off = OPTS_GET(opts, ref_ctr_offset, 0);
	pe_opts.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0);

	pfd = perf_event_open_probe(true /* uprobe */, retprobe, binary_path,
				    func_offset, pid, ref_ctr_off);
	if (pfd < 0) {
		pr_warn("prog '%s': failed to create %s '%s:0x%zx' perf event: %s\n",
			prog->name, retprobe ? "uretprobe" : "uprobe",
			binary_path, func_offset,
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(pfd);
	}
	link = bpf_program__attach_perf_event_opts(prog, pfd, &pe_opts);
	err = libbpf_get_error(link);
	if (err) {
		close(pfd);
		pr_warn("prog '%s': failed to attach to %s '%s:0x%zx': %s\n",
			prog->name, retprobe ? "uretprobe" : "uprobe",
			binary_path, func_offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(err);
	}
	return link;
}

struct bpf_link *bpf_program__attach_uprobe(struct bpf_program *prog,
					    bool retprobe, pid_t pid,
					    const char *binary_path,
					    size_t func_offset)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, opts, .retprobe = retprobe);

	return bpf_program__attach_uprobe_opts(prog, pid, binary_path, func_offset, &opts);
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

struct bpf_link *bpf_program__attach_tracepoint_opts(struct bpf_program *prog,
						     const char *tp_category,
						     const char *tp_name,
						     const struct bpf_tracepoint_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, pe_opts);
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int pfd, err;

	if (!OPTS_VALID(opts, bpf_tracepoint_opts))
		return libbpf_err_ptr(-EINVAL);

	pe_opts.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0);

	pfd = perf_event_open_tracepoint(tp_category, tp_name);
	if (pfd < 0) {
		pr_warn("prog '%s': failed to create tracepoint '%s/%s' perf event: %s\n",
			prog->name, tp_category, tp_name,
			libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(pfd);
	}
	link = bpf_program__attach_perf_event_opts(prog, pfd, &pe_opts);
	err = libbpf_get_error(link);
	if (err) {
		close(pfd);
		pr_warn("prog '%s': failed to attach to tracepoint '%s/%s': %s\n",
			prog->name, tp_category, tp_name,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(err);
	}
	return link;
}

struct bpf_link *bpf_program__attach_tracepoint(struct bpf_program *prog,
						const char *tp_category,
						const char *tp_name)
{
	return bpf_program__attach_tracepoint_opts(prog, tp_category, tp_name, NULL);
}

static struct bpf_link *attach_tp(const struct bpf_sec_def *sec,
				  struct bpf_program *prog)
{
	char *sec_name, *tp_cat, *tp_name;
	struct bpf_link *link;

	sec_name = strdup(prog->sec_name);
	if (!sec_name)
		return libbpf_err_ptr(-ENOMEM);

	/* extract "tp/<category>/<name>" */
	tp_cat = sec_name + sec->len;
	tp_name = strchr(tp_cat, '/');
	if (!tp_name) {
		free(sec_name);
		return libbpf_err_ptr(-EINVAL);
	}
	*tp_name = '\0';
	tp_name++;

	link = bpf_program__attach_tracepoint(prog, tp_cat, tp_name);
	free(sec_name);
	return link;
}

struct bpf_link *bpf_program__attach_raw_tracepoint(struct bpf_program *prog,
						    const char *tp_name)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int prog_fd, pfd;

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->detach = &bpf_link__detach_fd;

	pfd = bpf_raw_tracepoint_open(tp_name, prog_fd);
	if (pfd < 0) {
		pfd = -errno;
		free(link);
		pr_warn("prog '%s': failed to attach to raw tracepoint '%s': %s\n",
			prog->name, tp_name, libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(pfd);
	}
	link->fd = pfd;
	return link;
}

static struct bpf_link *attach_raw_tp(const struct bpf_sec_def *sec,
				      struct bpf_program *prog)
{
	const char *tp_name = prog->sec_name + sec->len;

	return bpf_program__attach_raw_tracepoint(prog, tp_name);
}

/* Common logic for all BPF program types that attach to a btf_id */
static struct bpf_link *bpf_program__attach_btf_id(struct bpf_program *prog)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int prog_fd, pfd;

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->detach = &bpf_link__detach_fd;

	pfd = bpf_raw_tracepoint_open(NULL, prog_fd);
	if (pfd < 0) {
		pfd = -errno;
		free(link);
		pr_warn("prog '%s': failed to attach: %s\n",
			prog->name, libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(pfd);
	}
	link->fd = pfd;
	return (struct bpf_link *)link;
}

struct bpf_link *bpf_program__attach_trace(struct bpf_program *prog)
{
	return bpf_program__attach_btf_id(prog);
}

struct bpf_link *bpf_program__attach_lsm(struct bpf_program *prog)
{
	return bpf_program__attach_btf_id(prog);
}

static struct bpf_link *attach_trace(const struct bpf_sec_def *sec,
				     struct bpf_program *prog)
{
	return bpf_program__attach_trace(prog);
}

static struct bpf_link *attach_lsm(const struct bpf_sec_def *sec,
				   struct bpf_program *prog)
{
	return bpf_program__attach_lsm(prog);
}

static struct bpf_link *
bpf_program__attach_fd(struct bpf_program *prog, int target_fd, int btf_id,
		       const char *target_name)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, opts,
			    .target_btf_id = btf_id);
	enum bpf_attach_type attach_type;
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int prog_fd, link_fd;

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->detach = &bpf_link__detach_fd;

	attach_type = bpf_program__get_expected_attach_type(prog);
	link_fd = bpf_link_create(prog_fd, target_fd, attach_type, &opts);
	if (link_fd < 0) {
		link_fd = -errno;
		free(link);
		pr_warn("prog '%s': failed to attach to %s: %s\n",
			prog->name, target_name,
			libbpf_strerror_r(link_fd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(link_fd);
	}
	link->fd = link_fd;
	return link;
}

struct bpf_link *
bpf_program__attach_cgroup(struct bpf_program *prog, int cgroup_fd)
{
	return bpf_program__attach_fd(prog, cgroup_fd, 0, "cgroup");
}

struct bpf_link *
bpf_program__attach_netns(struct bpf_program *prog, int netns_fd)
{
	return bpf_program__attach_fd(prog, netns_fd, 0, "netns");
}

struct bpf_link *bpf_program__attach_xdp(struct bpf_program *prog, int ifindex)
{
	/* target_fd/target_ifindex use the same field in LINK_CREATE */
	return bpf_program__attach_fd(prog, ifindex, 0, "xdp");
}

struct bpf_link *bpf_program__attach_freplace(struct bpf_program *prog,
					      int target_fd,
					      const char *attach_func_name)
{
	int btf_id;

	if (!!target_fd != !!attach_func_name) {
		pr_warn("prog '%s': supply none or both of target_fd and attach_func_name\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	if (prog->type != BPF_PROG_TYPE_EXT) {
		pr_warn("prog '%s': only BPF_PROG_TYPE_EXT can attach as freplace",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	if (target_fd) {
		btf_id = libbpf_find_prog_btf_id(attach_func_name, target_fd);
		if (btf_id < 0)
			return libbpf_err_ptr(btf_id);

		return bpf_program__attach_fd(prog, target_fd, btf_id, "freplace");
	} else {
		/* no target, so use raw_tracepoint_open for compatibility
		 * with old kernels
		 */
		return bpf_program__attach_trace(prog);
	}
}

struct bpf_link *
bpf_program__attach_iter(struct bpf_program *prog,
			 const struct bpf_iter_attach_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_link_create_opts, link_create_opts);
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int prog_fd, link_fd;
	__u32 target_fd = 0;

	if (!OPTS_VALID(opts, bpf_iter_attach_opts))
		return libbpf_err_ptr(-EINVAL);

	link_create_opts.iter_info = OPTS_GET(opts, link_info, (void *)0);
	link_create_opts.iter_info_len = OPTS_GET(opts, link_info_len, 0);

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->detach = &bpf_link__detach_fd;

	link_fd = bpf_link_create(prog_fd, target_fd, BPF_TRACE_ITER,
				  &link_create_opts);
	if (link_fd < 0) {
		link_fd = -errno;
		free(link);
		pr_warn("prog '%s': failed to attach to iterator: %s\n",
			prog->name, libbpf_strerror_r(link_fd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(link_fd);
	}
	link->fd = link_fd;
	return link;
}

static struct bpf_link *attach_iter(const struct bpf_sec_def *sec,
				    struct bpf_program *prog)
{
	return bpf_program__attach_iter(prog, NULL);
}

struct bpf_link *bpf_program__attach(struct bpf_program *prog)
{
	const struct bpf_sec_def *sec_def;

	sec_def = find_sec_def(prog->sec_name);
	if (!sec_def || !sec_def->attach_fn)
		return libbpf_err_ptr(-ESRCH);

	return sec_def->attach_fn(sec_def, prog);
}

static int bpf_link__detach_struct_ops(struct bpf_link *link)
{
	__u32 zero = 0;

	if (bpf_map_delete_elem(link->fd, &zero))
		return -errno;

	return 0;
}

struct bpf_link *bpf_map__attach_struct_ops(struct bpf_map *map)
{
	struct bpf_struct_ops *st_ops;
	struct bpf_link *link;
	__u32 i, zero = 0;
	int err;

	if (!bpf_map__is_struct_ops(map) || map->fd == -1)
		return libbpf_err_ptr(-EINVAL);

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-EINVAL);

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
		return libbpf_err_ptr(err);
	}

	link->detach = bpf_link__detach_struct_ops;
	link->fd = map->fd;

	return link;
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
	return libbpf_err(ret);
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

	if (IS_ERR_OR_NULL(pb))
		return;
	if (pb->cpu_bufs) {
		for (i = 0; i < pb->cpu_cnt; i++) {
			struct perf_cpu_buf *cpu_buf = pb->cpu_bufs[i];

			if (!cpu_buf)
				continue;

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

	attr.config = PERF_COUNT_SW_BPF_OUTPUT;
	attr.type = PERF_TYPE_SOFTWARE;
	attr.sample_type = PERF_SAMPLE_RAW;
	attr.sample_period = 1;
	attr.wakeup_events = 1;

	p.attr = &attr;
	p.sample_cb = opts ? opts->sample_cb : NULL;
	p.lost_cb = opts ? opts->lost_cb : NULL;
	p.ctx = opts ? opts->ctx : NULL;

	return libbpf_ptr(__perf_buffer__new(map_fd, page_cnt, &p));
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

	return libbpf_ptr(__perf_buffer__new(map_fd, page_cnt, &p));
}

static struct perf_buffer *__perf_buffer__new(int map_fd, size_t page_cnt,
					      struct perf_buffer_params *p)
{
	const char *online_cpus_file = "/sys/devices/system/cpu/online";
	struct bpf_map_info map;
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

	/* best-effort sanity checks */
	memset(&map, 0, sizeof(map));
	map_info_len = sizeof(map);
	err = bpf_obj_get_info_by_fd(map_fd, &map, &map_info_len);
	if (err) {
		err = -errno;
		/* if BPF_OBJ_GET_INFO_BY_FD is supported, will return
		 * -EBADFD, -EFAULT, or -E2BIG on real error
		 */
		if (err != -EINVAL) {
			pr_warn("failed to get map info for map FD %d: %s\n",
				map_fd, libbpf_strerror_r(err, msg, sizeof(msg)));
			return ERR_PTR(err);
		}
		pr_debug("failed to get map info for FD %d; API not supported? Ignoring...\n",
			 map_fd);
	} else {
		if (map.type != BPF_MAP_TYPE_PERF_EVENT_ARRAY) {
			pr_warn("map '%s' should be BPF_MAP_TYPE_PERF_EVENT_ARRAY\n",
				map.name);
			return ERR_PTR(-EINVAL);
		}
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
		if (map.max_entries && map.max_entries < pb->cpu_cnt)
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
	char data[];
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

int perf_buffer__epoll_fd(const struct perf_buffer *pb)
{
	return pb->epoll_fd;
}

int perf_buffer__poll(struct perf_buffer *pb, int timeout_ms)
{
	int i, cnt, err;

	cnt = epoll_wait(pb->epoll_fd, pb->events, pb->cpu_cnt, timeout_ms);
	if (cnt < 0)
		return -errno;

	for (i = 0; i < cnt; i++) {
		struct perf_cpu_buf *cpu_buf = pb->events[i].data.ptr;

		err = perf_buffer__process_records(pb, cpu_buf);
		if (err) {
			pr_warn("error while processing records: %d\n", err);
			return libbpf_err(err);
		}
	}
	return cnt;
}

/* Return number of PERF_EVENT_ARRAY map slots set up by this perf_buffer
 * manager.
 */
size_t perf_buffer__buffer_cnt(const struct perf_buffer *pb)
{
	return pb->cpu_cnt;
}

/*
 * Return perf_event FD of a ring buffer in *buf_idx* slot of
 * PERF_EVENT_ARRAY BPF map. This FD can be polled for new data using
 * select()/poll()/epoll() Linux syscalls.
 */
int perf_buffer__buffer_fd(const struct perf_buffer *pb, size_t buf_idx)
{
	struct perf_cpu_buf *cpu_buf;

	if (buf_idx >= pb->cpu_cnt)
		return libbpf_err(-EINVAL);

	cpu_buf = pb->cpu_bufs[buf_idx];
	if (!cpu_buf)
		return libbpf_err(-ENOENT);

	return cpu_buf->fd;
}

/*
 * Consume data from perf ring buffer corresponding to slot *buf_idx* in
 * PERF_EVENT_ARRAY BPF map without waiting/polling. If there is no data to
 * consume, do nothing and return success.
 * Returns:
 *   - 0 on success;
 *   - <0 on failure.
 */
int perf_buffer__consume_buffer(struct perf_buffer *pb, size_t buf_idx)
{
	struct perf_cpu_buf *cpu_buf;

	if (buf_idx >= pb->cpu_cnt)
		return libbpf_err(-EINVAL);

	cpu_buf = pb->cpu_bufs[buf_idx];
	if (!cpu_buf)
		return libbpf_err(-ENOENT);

	return perf_buffer__process_records(pb, cpu_buf);
}

int perf_buffer__consume(struct perf_buffer *pb)
{
	int i, err;

	for (i = 0; i < pb->cpu_cnt; i++) {
		struct perf_cpu_buf *cpu_buf = pb->cpu_bufs[i];

		if (!cpu_buf)
			continue;

		err = perf_buffer__process_records(pb, cpu_buf);
		if (err) {
			pr_warn("perf_buffer: failed to process records in buffer #%d: %d\n", i, err);
			return libbpf_err(err);
		}
	}
	return 0;
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
		return libbpf_err_ptr(-EINVAL);

	/* step 1: get array dimensions */
	err = bpf_obj_get_info_by_fd(fd, &info, &info_len);
	if (err) {
		pr_debug("can't get prog info: %s", strerror(errno));
		return libbpf_err_ptr(-EFAULT);
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
		return libbpf_err_ptr(-ENOMEM);

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
		return libbpf_err_ptr(-EFAULT);
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
	int btf_obj_fd = 0, btf_id = 0, err;

	if (!prog || attach_prog_fd < 0 || !attach_func_name)
		return libbpf_err(-EINVAL);

	if (prog->obj->loaded)
		return libbpf_err(-EINVAL);

	if (attach_prog_fd) {
		btf_id = libbpf_find_prog_btf_id(attach_func_name,
						 attach_prog_fd);
		if (btf_id < 0)
			return libbpf_err(btf_id);
	} else {
		/* load btf_vmlinux, if not yet */
		err = bpf_object__load_vmlinux_btf(prog->obj, true);
		if (err)
			return libbpf_err(err);
		err = find_kernel_btf_id(prog->obj, attach_func_name,
					 prog->expected_attach_type,
					 &btf_obj_fd, &btf_id);
		if (err)
			return libbpf_err(err);
	}

	prog->attach_btf_id = btf_id;
	prog->attach_btf_obj_fd = btf_obj_fd;
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
		return libbpf_err(err);

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
	int i, err;

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
	err = libbpf_get_error(obj);
	if (err) {
		pr_warn("failed to initialize skeleton BPF object '%s': %d\n",
			s->name, err);
		return libbpf_err(err);
	}

	*s->obj = obj;

	for (i = 0; i < s->map_cnt; i++) {
		struct bpf_map **map = s->maps[i].map;
		const char *name = s->maps[i].name;
		void **mmaped = s->maps[i].mmaped;

		*map = bpf_object__find_map_by_name(obj, name);
		if (!*map) {
			pr_warn("failed to find skeleton map '%s'\n", name);
			return libbpf_err(-ESRCH);
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
			return libbpf_err(-ESRCH);
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
		return libbpf_err(err);
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
			return libbpf_err(err);
		}
	}

	return 0;
}

int bpf_object__attach_skeleton(struct bpf_object_skeleton *s)
{
	int i, err;

	for (i = 0; i < s->prog_cnt; i++) {
		struct bpf_program *prog = *s->progs[i].prog;
		struct bpf_link **link = s->progs[i].link;
		const struct bpf_sec_def *sec_def;

		if (!prog->load)
			continue;

		sec_def = find_sec_def(prog->sec_name);
		if (!sec_def || !sec_def->attach_fn)
			continue;

		*link = sec_def->attach_fn(sec_def, prog);
		err = libbpf_get_error(*link);
		if (err) {
			pr_warn("failed to auto-attach program '%s': %d\n",
				bpf_program__name(prog), err);
			return libbpf_err(err);
		}
	}

	return 0;
}

void bpf_object__detach_skeleton(struct bpf_object_skeleton *s)
{
	int i;

	for (i = 0; i < s->prog_cnt; i++) {
		struct bpf_link **link = s->progs[i].link;

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
