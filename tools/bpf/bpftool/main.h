/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2017-2018 Netronome Systems, Inc. */

#ifndef __BPF_TOOL_H
#define __BPF_TOOL_H

/* BFD and kernel.h both define GCC_VERSION, differently */
#undef GCC_VERSION
#include <stdbool.h>
#include <stdio.h>
#include <linux/bpf.h>
#include <linux/compiler.h>
#include <linux/kernel.h>
#include <linux/hashtable.h>
#include <tools/libc_compat.h>

#include "json_writer.h"

#define ptr_to_u64(ptr)	((__u64)(unsigned long)(ptr))

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
	"PROG := { id PROG_ID | pinned FILE | tag PROG_TAG }"
#define HELP_SPEC_OPTIONS						\
	"OPTIONS := { {-j|--json} [{-p|--pretty}] | {-f|--bpffs} |\n"	\
	"\t            {-m|--mapcompat} | {-n|--nomount} }"
#define HELP_SPEC_MAP							\
	"MAP := { id MAP_ID | pinned FILE }"

static const char * const prog_type_name[] = {
	[BPF_PROG_TYPE_UNSPEC]			= "unspec",
	[BPF_PROG_TYPE_SOCKET_FILTER]		= "socket_filter",
	[BPF_PROG_TYPE_KPROBE]			= "kprobe",
	[BPF_PROG_TYPE_SCHED_CLS]		= "sched_cls",
	[BPF_PROG_TYPE_SCHED_ACT]		= "sched_act",
	[BPF_PROG_TYPE_TRACEPOINT]		= "tracepoint",
	[BPF_PROG_TYPE_XDP]			= "xdp",
	[BPF_PROG_TYPE_PERF_EVENT]		= "perf_event",
	[BPF_PROG_TYPE_CGROUP_SKB]		= "cgroup_skb",
	[BPF_PROG_TYPE_CGROUP_SOCK]		= "cgroup_sock",
	[BPF_PROG_TYPE_LWT_IN]			= "lwt_in",
	[BPF_PROG_TYPE_LWT_OUT]			= "lwt_out",
	[BPF_PROG_TYPE_LWT_XMIT]		= "lwt_xmit",
	[BPF_PROG_TYPE_SOCK_OPS]		= "sock_ops",
	[BPF_PROG_TYPE_SK_SKB]			= "sk_skb",
	[BPF_PROG_TYPE_CGROUP_DEVICE]		= "cgroup_device",
	[BPF_PROG_TYPE_SK_MSG]			= "sk_msg",
	[BPF_PROG_TYPE_RAW_TRACEPOINT]		= "raw_tracepoint",
	[BPF_PROG_TYPE_CGROUP_SOCK_ADDR]	= "cgroup_sock_addr",
	[BPF_PROG_TYPE_LWT_SEG6LOCAL]		= "lwt_seg6local",
	[BPF_PROG_TYPE_LIRC_MODE2]		= "lirc_mode2",
	[BPF_PROG_TYPE_SK_REUSEPORT]		= "sk_reuseport",
	[BPF_PROG_TYPE_FLOW_DISSECTOR]		= "flow_dissector",
};

enum bpf_obj_type {
	BPF_OBJ_UNKNOWN,
	BPF_OBJ_PROG,
	BPF_OBJ_MAP,
};

extern const char *bin_name;

extern json_writer_t *json_wtr;
extern bool json_output;
extern bool show_pinned;
extern bool block_mount;
extern int bpf_flags;
extern struct pinned_obj_table prog_table;
extern struct pinned_obj_table map_table;

void p_err(const char *fmt, ...);
void p_info(const char *fmt, ...);

bool is_prefix(const char *pfx, const char *str);
void fprint_hex(FILE *f, void *arg, unsigned int n, const char *sep);
void usage(void) __noreturn;

void set_max_rlimit(void);

int mount_tracefs(const char *target);

struct pinned_obj_table {
	DECLARE_HASHTABLE(table, 16);
};

struct pinned_obj {
	__u32 id;
	char *path;
	struct hlist_node hash;
};

struct btf;
struct bpf_line_info;

int build_pinned_obj_table(struct pinned_obj_table *table,
			   enum bpf_obj_type type);
void delete_pinned_obj_table(struct pinned_obj_table *tab);
void print_dev_plain(__u32 ifindex, __u64 ns_dev, __u64 ns_inode);
void print_dev_json(__u32 ifindex, __u64 ns_dev, __u64 ns_inode);

struct cmd {
	const char *cmd;
	int (*func)(int argc, char **argv);
};

int cmd_select(const struct cmd *cmds, int argc, char **argv,
	       int (*help)(int argc, char **argv));

int get_fd_type(int fd);
const char *get_fd_type_name(enum bpf_obj_type type);
char *get_fdinfo(int fd, const char *key);
int open_obj_pinned(char *path, bool quiet);
int open_obj_pinned_any(char *path, enum bpf_obj_type exp_type);
int mount_bpffs_for_pin(const char *name);
int do_pin_any(int argc, char **argv, int (*get_fd_by_id)(__u32));
int do_pin_fd(int fd, const char *name);

int do_prog(int argc, char **arg);
int do_map(int argc, char **arg);
int do_event_pipe(int argc, char **argv);
int do_cgroup(int argc, char **arg);
int do_perf(int argc, char **arg);
int do_net(int argc, char **arg);
int do_tracelog(int argc, char **arg);

int parse_u32_arg(int *argc, char ***argv, __u32 *val, const char *what);
int prog_parse_fd(int *argc, char ***argv);
int map_parse_fd(int *argc, char ***argv);
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
#endif
