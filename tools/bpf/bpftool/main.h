/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef __BPF_TOOL_H
#define __BPF_TOOL_H

/* BFD and kernel.h both define GCC_VERSION, differently */
#undef GCC_VERSION
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <linux/bpf.h>
#include <linux/compiler.h>
#include <linux/kernel.h>

#include <bpf/hashmap.h>
#include <bpf/libbpf.h>

#include "json_writer.h"

/* Make sure we do not use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

static inline __u64 ptr_to_u64(const void *ptr)
{
	return (__u64)(unsigned long)ptr;
}

static inline void *u64_to_ptr(__u64 ptr)
{
	return (void *)(unsigned long)ptr;
}

#define NEXT_ARG()	({ argc--; argv++; if (argc < 0) usage(); })
#define NEXT_ARGP()	({ (*argc)--; (*argv)++; if (*argc < 0) usage(); })
#define BAD_ARG()	({ p_err("what is '%s'?", *argv); -1; })
#define GET_ARG()	({ argc--; *argv++; })
#define REQ_ARGS(cnt)							\
	({								\
		int _cnt = (cnt);					\
		bool _res;						\
									\
		if (argc < _cnt) {					\
			p_err("'%s' needs at least %d arguments, %d found", \
			      argv[-1], _cnt, argc);			\
			_res = false;					\
		} else {						\
			_res = true;					\
		}							\
		_res;							\
	})

#define ERR_MAX_LEN	1024

#define BPF_TAG_FMT	"%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx%02hhx"

#define HELP_SPEC_PROGRAM						\
	"PROG := { id PROG_ID | pinned FILE | tag PROG_TAG | name PROG_NAME }"
#define HELP_SPEC_OPTIONS						\
	"OPTIONS := { {-j|--json} [{-p|--pretty}] | {-d|--debug} | {-l|--legacy}"
#define HELP_SPEC_MAP							\
	"MAP := { id MAP_ID | pinned FILE | name MAP_NAME }"
#define HELP_SPEC_LINK							\
	"LINK := { id LINK_ID | pinned FILE }"

extern const char * const prog_type_name[];
extern const size_t prog_type_name_size;

extern const char * const attach_type_name[__MAX_BPF_ATTACH_TYPE];

extern const char * const map_type_name[];
extern const size_t map_type_name_size;

/* keep in sync with the definition in skeleton/pid_iter.bpf.c */
enum bpf_obj_type {
	BPF_OBJ_UNKNOWN,
	BPF_OBJ_PROG,
	BPF_OBJ_MAP,
	BPF_OBJ_LINK,
	BPF_OBJ_BTF,
};

extern const char *bin_name;

extern json_writer_t *json_wtr;
extern bool json_output;
extern bool show_pinned;
extern bool show_pids;
extern bool block_mount;
extern bool verifier_logs;
extern bool relaxed_maps;
extern bool use_loader;
extern bool legacy_libbpf;
extern struct btf *base_btf;
extern struct hashmap *refs_table;

void __printf(1, 2) p_err(const char *fmt, ...);
void __printf(1, 2) p_info(const char *fmt, ...);

bool is_prefix(const char *pfx, const char *str);
int detect_common_prefix(const char *arg, ...);
void fprint_hex(FILE *f, void *arg, unsigned int n, const char *sep);
void usage(void) __noreturn;

void set_max_rlimit(void);

int mount_tracefs(const char *target);

struct obj_ref {
	int pid;
	char comm[16];
};

struct obj_refs {
	int ref_cnt;
	bool has_bpf_cookie;
	struct obj_ref *refs;
	__u64 bpf_cookie;
};

struct btf;
struct bpf_line_info;

int build_pinned_obj_table(struct hashmap *table,
			   enum bpf_obj_type type);
void delete_pinned_obj_table(struct hashmap *table);
__weak int build_obj_refs_table(struct hashmap **table,
				enum bpf_obj_type type);
__weak void delete_obj_refs_table(struct hashmap *table);
__weak void emit_obj_refs_json(struct hashmap *table, __u32 id,
			       json_writer_t *json_wtr);
__weak void emit_obj_refs_plain(struct hashmap *table, __u32 id,
				const char *prefix);
void print_dev_plain(__u32 ifindex, __u64 ns_dev, __u64 ns_inode);
void print_dev_json(__u32 ifindex, __u64 ns_dev, __u64 ns_inode);

struct cmd {
	const char *cmd;
	int (*func)(int argc, char **argv);
};

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv));

#define MAX_PROG_FULL_NAME 128
void get_prog_full_name(const struct bpf_prog_info *prog_info, int prog_fd,
			char *name_buff, size_t buff_len);

int get_fd_type(int fd);
const char *get_fd_type_name(enum bpf_obj_type type);
char *get_fdinfo(int fd, const char *key);
int open_obj_pinned(const char *path, bool quiet);
int open_obj_pinned_any(const char *path, enum bpf_obj_type exp_type);
int mount_bpffs_for_pin(const char *name);
int do_pin_any(int argc, char **argv, int (*get_fd_by_id)(int *, char ***));
int do_pin_fd(int fd, const char *name);

/* commands available in bootstrap mode */
int do_gen(int argc, char **argv);
int do_btf(int argc, char **argv);

/* non-bootstrap only commands */
int do_prog(int argc, char **arg) __weak;
int do_map(int argc, char **arg) __weak;
int do_link(int argc, char **arg) __weak;
int do_event_pipe(int argc, char **argv) __weak;
int do_cgroup(int argc, char **arg) __weak;
int do_perf(int argc, char **arg) __weak;
int do_net(int argc, char **arg) __weak;
int do_tracelog(int argc, char **arg) __weak;
int do_feature(int argc, char **argv) __weak;
int do_struct_ops(int argc, char **argv) __weak;
int do_iter(int argc, char **argv) __weak;

int parse_u32_arg(int *argc, char ***argv, __u32 *val, const char *what);
int prog_parse_fd(int *argc, char ***argv);
int prog_parse_fds(int *argc, char ***argv, int **fds);
int map_parse_fd(int *argc, char ***argv);
int map_parse_fds(int *argc, char ***argv, int **fds);
int map_parse_fd_and_info(int *argc, char ***argv, void *info, __u32 *info_len);

struct bpf_prog_linfo;
#ifdef HAVE_LIBBFD_SUPPORT
void disasm_print_insn(unsigned char *image, ssize_t len, int opcodes,
		       const char *arch, const char *disassembler_options,
		       const struct btf *btf,
		       const struct bpf_prog_linfo *prog_linfo,
		       __u64 func_ksym, unsigned int func_idx,
		       bool linum);
int disasm_init(void);
#else
static inline
void disasm_print_insn(unsigned char *image, ssize_t len, int opcodes,
		       const char *arch, const char *disassembler_options,
		       const struct btf *btf,
		       const struct bpf_prog_linfo *prog_linfo,
		       __u64 func_ksym, unsigned int func_idx,
		       bool linum)
{
}
static inline int disasm_init(void)
{
	p_err("No libbfd support");
	return -1;
}
#endif
void print_data_json(uint8_t *data, size_t len);
void print_hex_data_json(uint8_t *data, size_t len);

unsigned int get_page_size(void);
unsigned int get_possible_cpus(void);
const char *
ifindex_to_bfd_params(__u32 ifindex, __u64 ns_dev, __u64 ns_ino,
		      const char **opt);

struct btf_dumper {
	const struct btf *btf;
	json_writer_t *jw;
	bool is_plain_text;
	bool prog_id_as_func_ptr;
};

/* btf_dumper_type - print data along with type information
 * @d: an instance containing context for dumping types
 * @type_id: index in btf->types array. this points to the type to be dumped
 * @data: pointer the actual data, i.e. the values to be printed
 *
 * Returns zero on success and negative error code otherwise
 */
int btf_dumper_type(const struct btf_dumper *d, __u32 type_id,
		    const void *data);
void btf_dumper_type_only(const struct btf *btf, __u32 func_type_id,
			  char *func_only, int size);

void btf_dump_linfo_plain(const struct btf *btf,
			  const struct bpf_line_info *linfo,
			  const char *prefix, bool linum);
void btf_dump_linfo_json(const struct btf *btf,
			 const struct bpf_line_info *linfo, bool linum);

struct nlattr;
struct ifinfomsg;
struct tcmsg;
int do_xdp_dump(struct ifinfomsg *ifinfo, struct nlattr **tb);
int do_filter_dump(struct tcmsg *ifinfo, struct nlattr **tb, const char *kind,
		   const char *devname, int ifindex);

int print_all_levels(__maybe_unused enum libbpf_print_level level,
		     const char *format, va_list args);

size_t hash_fn_for_key_as_id(const void *key, void *ctx);
bool equal_fn_for_key_as_id(const void *k1, const void *k2, void *ctx);

static inline void *u32_as_hash_field(__u32 x)
{
	return (void *)(uintptr_t)x;
}

static inline __u32 hash_field_as_u32(const void *x)
{
	return (__u32)(uintptr_t)x;
}

static inline bool hashmap__empty(struct hashmap *map)
{
	return map ? hashmap__size(map) == 0 : true;
}

#endif
