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
#include <linux/limits.h>
#include <linux/perf_event.h>
#include <linux/bpf_perf_event.h>
#include <linux/ring_buffer.h>
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
#include "zip.h"

#ifndef BPF_FS_MAGIC
#define BPF_FS_MAGIC		0xcafe4a11
#endif

#define BPF_FS_DEFAULT_PATH "/sys/fs/bpf"

#define BPF_INSN_SZ (sizeof(struct bpf_insn))

/* vsprintf() in __base_pr() uses nonliteral format string. It may break
 * compilation if user enables corresponding warning. Disable it explicitly.
 */
#pragma GCC diagnostic ignored "-Wformat-nonliteral"

#define __printf(a, b)	__attribute__((format(printf, a, b)))

static struct bpf_map *bpf_object__add_map(struct bpf_object *obj);
static bool prog_is_subprog(const struct bpf_object *obj, const struct bpf_program *prog);
static int map_set_def_max_entries(struct bpf_map *map);

static const char * const attach_type_name[] = {
	[BPF_CGROUP_INET_INGRESS]	= "cgroup_inet_ingress",
	[BPF_CGROUP_INET_EGRESS]	= "cgroup_inet_egress",
	[BPF_CGROUP_INET_SOCK_CREATE]	= "cgroup_inet_sock_create",
	[BPF_CGROUP_INET_SOCK_RELEASE]	= "cgroup_inet_sock_release",
	[BPF_CGROUP_SOCK_OPS]		= "cgroup_sock_ops",
	[BPF_CGROUP_DEVICE]		= "cgroup_device",
	[BPF_CGROUP_INET4_BIND]		= "cgroup_inet4_bind",
	[BPF_CGROUP_INET6_BIND]		= "cgroup_inet6_bind",
	[BPF_CGROUP_INET4_CONNECT]	= "cgroup_inet4_connect",
	[BPF_CGROUP_INET6_CONNECT]	= "cgroup_inet6_connect",
	[BPF_CGROUP_UNIX_CONNECT]       = "cgroup_unix_connect",
	[BPF_CGROUP_INET4_POST_BIND]	= "cgroup_inet4_post_bind",
	[BPF_CGROUP_INET6_POST_BIND]	= "cgroup_inet6_post_bind",
	[BPF_CGROUP_INET4_GETPEERNAME]	= "cgroup_inet4_getpeername",
	[BPF_CGROUP_INET6_GETPEERNAME]	= "cgroup_inet6_getpeername",
	[BPF_CGROUP_UNIX_GETPEERNAME]	= "cgroup_unix_getpeername",
	[BPF_CGROUP_INET4_GETSOCKNAME]	= "cgroup_inet4_getsockname",
	[BPF_CGROUP_INET6_GETSOCKNAME]	= "cgroup_inet6_getsockname",
	[BPF_CGROUP_UNIX_GETSOCKNAME]	= "cgroup_unix_getsockname",
	[BPF_CGROUP_UDP4_SENDMSG]	= "cgroup_udp4_sendmsg",
	[BPF_CGROUP_UDP6_SENDMSG]	= "cgroup_udp6_sendmsg",
	[BPF_CGROUP_UNIX_SENDMSG]	= "cgroup_unix_sendmsg",
	[BPF_CGROUP_SYSCTL]		= "cgroup_sysctl",
	[BPF_CGROUP_UDP4_RECVMSG]	= "cgroup_udp4_recvmsg",
	[BPF_CGROUP_UDP6_RECVMSG]	= "cgroup_udp6_recvmsg",
	[BPF_CGROUP_UNIX_RECVMSG]	= "cgroup_unix_recvmsg",
	[BPF_CGROUP_GETSOCKOPT]		= "cgroup_getsockopt",
	[BPF_CGROUP_SETSOCKOPT]		= "cgroup_setsockopt",
	[BPF_SK_SKB_STREAM_PARSER]	= "sk_skb_stream_parser",
	[BPF_SK_SKB_STREAM_VERDICT]	= "sk_skb_stream_verdict",
	[BPF_SK_SKB_VERDICT]		= "sk_skb_verdict",
	[BPF_SK_MSG_VERDICT]		= "sk_msg_verdict",
	[BPF_LIRC_MODE2]		= "lirc_mode2",
	[BPF_FLOW_DISSECTOR]		= "flow_dissector",
	[BPF_TRACE_RAW_TP]		= "trace_raw_tp",
	[BPF_TRACE_FENTRY]		= "trace_fentry",
	[BPF_TRACE_FEXIT]		= "trace_fexit",
	[BPF_MODIFY_RETURN]		= "modify_return",
	[BPF_LSM_MAC]			= "lsm_mac",
	[BPF_LSM_CGROUP]		= "lsm_cgroup",
	[BPF_SK_LOOKUP]			= "sk_lookup",
	[BPF_TRACE_ITER]		= "trace_iter",
	[BPF_XDP_DEVMAP]		= "xdp_devmap",
	[BPF_XDP_CPUMAP]		= "xdp_cpumap",
	[BPF_XDP]			= "xdp",
	[BPF_SK_REUSEPORT_SELECT]	= "sk_reuseport_select",
	[BPF_SK_REUSEPORT_SELECT_OR_MIGRATE]	= "sk_reuseport_select_or_migrate",
	[BPF_PERF_EVENT]		= "perf_event",
	[BPF_TRACE_KPROBE_MULTI]	= "trace_kprobe_multi",
	[BPF_STRUCT_OPS]		= "struct_ops",
	[BPF_NETFILTER]			= "netfilter",
	[BPF_TCX_INGRESS]		= "tcx_ingress",
	[BPF_TCX_EGRESS]		= "tcx_egress",
	[BPF_TRACE_UPROBE_MULTI]	= "trace_uprobe_multi",
	[BPF_NETKIT_PRIMARY]		= "netkit_primary",
	[BPF_NETKIT_PEER]		= "netkit_peer",
	[BPF_TRACE_KPROBE_SESSION]	= "trace_kprobe_session",
};

static const char * const link_type_name[] = {
	[BPF_LINK_TYPE_UNSPEC]			= "unspec",
	[BPF_LINK_TYPE_RAW_TRACEPOINT]		= "raw_tracepoint",
	[BPF_LINK_TYPE_TRACING]			= "tracing",
	[BPF_LINK_TYPE_CGROUP]			= "cgroup",
	[BPF_LINK_TYPE_ITER]			= "iter",
	[BPF_LINK_TYPE_NETNS]			= "netns",
	[BPF_LINK_TYPE_XDP]			= "xdp",
	[BPF_LINK_TYPE_PERF_EVENT]		= "perf_event",
	[BPF_LINK_TYPE_KPROBE_MULTI]		= "kprobe_multi",
	[BPF_LINK_TYPE_STRUCT_OPS]		= "struct_ops",
	[BPF_LINK_TYPE_NETFILTER]		= "netfilter",
	[BPF_LINK_TYPE_TCX]			= "tcx",
	[BPF_LINK_TYPE_UPROBE_MULTI]		= "uprobe_multi",
	[BPF_LINK_TYPE_NETKIT]			= "netkit",
	[BPF_LINK_TYPE_SOCKMAP]			= "sockmap",
};

static const char * const map_type_name[] = {
	[BPF_MAP_TYPE_UNSPEC]			= "unspec",
	[BPF_MAP_TYPE_HASH]			= "hash",
	[BPF_MAP_TYPE_ARRAY]			= "array",
	[BPF_MAP_TYPE_PROG_ARRAY]		= "prog_array",
	[BPF_MAP_TYPE_PERF_EVENT_ARRAY]		= "perf_event_array",
	[BPF_MAP_TYPE_PERCPU_HASH]		= "percpu_hash",
	[BPF_MAP_TYPE_PERCPU_ARRAY]		= "percpu_array",
	[BPF_MAP_TYPE_STACK_TRACE]		= "stack_trace",
	[BPF_MAP_TYPE_CGROUP_ARRAY]		= "cgroup_array",
	[BPF_MAP_TYPE_LRU_HASH]			= "lru_hash",
	[BPF_MAP_TYPE_LRU_PERCPU_HASH]		= "lru_percpu_hash",
	[BPF_MAP_TYPE_LPM_TRIE]			= "lpm_trie",
	[BPF_MAP_TYPE_ARRAY_OF_MAPS]		= "array_of_maps",
	[BPF_MAP_TYPE_HASH_OF_MAPS]		= "hash_of_maps",
	[BPF_MAP_TYPE_DEVMAP]			= "devmap",
	[BPF_MAP_TYPE_DEVMAP_HASH]		= "devmap_hash",
	[BPF_MAP_TYPE_SOCKMAP]			= "sockmap",
	[BPF_MAP_TYPE_CPUMAP]			= "cpumap",
	[BPF_MAP_TYPE_XSKMAP]			= "xskmap",
	[BPF_MAP_TYPE_SOCKHASH]			= "sockhash",
	[BPF_MAP_TYPE_CGROUP_STORAGE]		= "cgroup_storage",
	[BPF_MAP_TYPE_REUSEPORT_SOCKARRAY]	= "reuseport_sockarray",
	[BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE]	= "percpu_cgroup_storage",
	[BPF_MAP_TYPE_QUEUE]			= "queue",
	[BPF_MAP_TYPE_STACK]			= "stack",
	[BPF_MAP_TYPE_SK_STORAGE]		= "sk_storage",
	[BPF_MAP_TYPE_STRUCT_OPS]		= "struct_ops",
	[BPF_MAP_TYPE_RINGBUF]			= "ringbuf",
	[BPF_MAP_TYPE_INODE_STORAGE]		= "inode_storage",
	[BPF_MAP_TYPE_TASK_STORAGE]		= "task_storage",
	[BPF_MAP_TYPE_BLOOM_FILTER]		= "bloom_filter",
	[BPF_MAP_TYPE_USER_RINGBUF]             = "user_ringbuf",
	[BPF_MAP_TYPE_CGRP_STORAGE]		= "cgrp_storage",
	[BPF_MAP_TYPE_ARENA]			= "arena",
};

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
	[BPF_PROG_TYPE_CGROUP_SYSCTL]		= "cgroup_sysctl",
	[BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE]	= "raw_tracepoint_writable",
	[BPF_PROG_TYPE_CGROUP_SOCKOPT]		= "cgroup_sockopt",
	[BPF_PROG_TYPE_TRACING]			= "tracing",
	[BPF_PROG_TYPE_STRUCT_OPS]		= "struct_ops",
	[BPF_PROG_TYPE_EXT]			= "ext",
	[BPF_PROG_TYPE_LSM]			= "lsm",
	[BPF_PROG_TYPE_SK_LOOKUP]		= "sk_lookup",
	[BPF_PROG_TYPE_SYSCALL]			= "syscall",
	[BPF_PROG_TYPE_NETFILTER]		= "netfilter",
};

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
	libbpf_print_fn_t old_print_fn;

	old_print_fn = __atomic_exchange_n(&__libbpf_pr, fn, __ATOMIC_RELAXED);

	return old_print_fn;
}

__printf(2, 3)
void libbpf_print(enum libbpf_print_level level, const char *format, ...)
{
	va_list args;
	int old_errno;
	libbpf_print_fn_t print_fn;

	print_fn = __atomic_load_n(&__libbpf_pr, __ATOMIC_RELAXED);
	if (!print_fn)
		return;

	old_errno = errno;

	va_start(args, format);
	__libbpf_pr(level, format, args);
	va_end(args);

	errno = old_errno;
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

int libbpf_set_strict_mode(enum libbpf_strict_mode mode)
{
	/* as of v1.0 libbpf_set_strict_mode() is a no-op */
	return 0;
}

__u32 libbpf_major_version(void)
{
	return LIBBPF_MAJOR_VERSION;
}

__u32 libbpf_minor_version(void)
{
	return LIBBPF_MINOR_VERSION;
}

const char *libbpf_version_string(void)
{
#define __S(X) #X
#define _S(X) __S(X)
	return  "v" _S(LIBBPF_MAJOR_VERSION) "." _S(LIBBPF_MINOR_VERSION);
#undef _S
#undef __S
}

enum reloc_type {
	RELO_LD64,
	RELO_CALL,
	RELO_DATA,
	RELO_EXTERN_LD64,
	RELO_EXTERN_CALL,
	RELO_SUBPROG_ADDR,
	RELO_CORE,
};

struct reloc_desc {
	enum reloc_type type;
	int insn_idx;
	union {
		const struct bpf_core_relo *core_relo; /* used when type == RELO_CORE */
		struct {
			int map_idx;
			int sym_off;
			int ext_idx;
		};
	};
};

/* stored as sec_def->cookie for all libbpf-supported SEC()s */
enum sec_def_flags {
	SEC_NONE = 0,
	/* expected_attach_type is optional, if kernel doesn't support that */
	SEC_EXP_ATTACH_OPT = 1,
	/* legacy, only used by libbpf_get_type_names() and
	 * libbpf_attach_type_by_name(), not used by libbpf itself at all.
	 * This used to be associated with cgroup (and few other) BPF programs
	 * that were attachable through BPF_PROG_ATTACH command. Pretty
	 * meaningless nowadays, though.
	 */
	SEC_ATTACHABLE = 2,
	SEC_ATTACHABLE_OPT = SEC_ATTACHABLE | SEC_EXP_ATTACH_OPT,
	/* attachment target is specified through BTF ID in either kernel or
	 * other BPF program's BTF object
	 */
	SEC_ATTACH_BTF = 4,
	/* BPF program type allows sleeping/blocking in kernel */
	SEC_SLEEPABLE = 8,
	/* BPF program support non-linear XDP buffer */
	SEC_XDP_FRAGS = 16,
	/* Setup proper attach type for usdt probes. */
	SEC_USDT = 32,
};

struct bpf_sec_def {
	char *sec;
	enum bpf_prog_type prog_type;
	enum bpf_attach_type expected_attach_type;
	long cookie;
	int handler_id;

	libbpf_prog_setup_fn_t prog_setup_fn;
	libbpf_prog_prepare_load_fn_t prog_prepare_load_fn;
	libbpf_prog_attach_fn_t prog_attach_fn;
};

/*
 * bpf_prog should be a better name but it has been used in
 * linux/filter.h.
 */
struct bpf_program {
	char *name;
	char *sec_name;
	size_t sec_idx;
	const struct bpf_sec_def *sec_def;
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

	/* BPF verifier log settings */
	char *log_buf;
	size_t log_size;
	__u32 log_level;

	struct bpf_object *obj;

	int fd;
	bool autoload;
	bool autoattach;
	bool sym_global;
	bool mark_btf_static;
	enum bpf_prog_type type;
	enum bpf_attach_type expected_attach_type;
	int exception_cb_idx;

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
#define STRUCT_OPS_LINK_SEC ".struct_ops.link"
#define ARENA_SEC ".addr_space.1"

enum libbpf_map_type {
	LIBBPF_MAP_UNSPEC,
	LIBBPF_MAP_DATA,
	LIBBPF_MAP_BSS,
	LIBBPF_MAP_RODATA,
	LIBBPF_MAP_KCONFIG,
};

struct bpf_map_def {
	unsigned int type;
	unsigned int key_size;
	unsigned int value_size;
	unsigned int max_entries;
	unsigned int map_flags;
};

struct bpf_map {
	struct bpf_object *obj;
	char *name;
	/* real_name is defined for special internal maps (.rodata*,
	 * .data*, .bss, .kconfig) and preserves their original ELF section
	 * name. This is important to be able to find corresponding BTF
	 * DATASEC information.
	 */
	char *real_name;
	int fd;
	int sec_idx;
	size_t sec_offset;
	int map_ifindex;
	int inner_map_fd;
	struct bpf_map_def def;
	__u32 numa_node;
	__u32 btf_var_idx;
	int mod_btf_fd;
	__u32 btf_key_type_id;
	__u32 btf_value_type_id;
	__u32 btf_vmlinux_value_type_id;
	enum libbpf_map_type libbpf_type;
	void *mmaped;
	struct bpf_struct_ops *st_ops;
	struct bpf_map *inner_map;
	void **init_slots;
	int init_slots_sz;
	char *pin_path;
	bool pinned;
	bool reused;
	bool autocreate;
	__u64 map_extra;
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
	char *essent_name;
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
			/* BTF fd index to be patched in for insn->off, this is
			 * 0 for vmlinux BTF, index in obj->fd_array for module
			 * BTF
			 */
			__s16 btf_fd_idx;
		} ksym;
	};
};

struct module_btf {
	struct btf *btf;
	char *name;
	__u32 id;
	int fd;
	int fd_array_idx;
};

enum sec_type {
	SEC_UNUSED = 0,
	SEC_RELO,
	SEC_BSS,
	SEC_DATA,
	SEC_RODATA,
	SEC_ST_OPS,
};

struct elf_sec_desc {
	enum sec_type sec_type;
	Elf64_Shdr *shdr;
	Elf_Data *data;
};

struct elf_state {
	int fd;
	const void *obj_buf;
	size_t obj_buf_sz;
	Elf *elf;
	Elf64_Ehdr *ehdr;
	Elf_Data *symbols;
	Elf_Data *arena_data;
	size_t shstrndx; /* section index for section name strings */
	size_t strtabidx;
	struct elf_sec_desc *secs;
	size_t sec_cnt;
	int btf_maps_shndx;
	__u32 btf_maps_sec_btf_id;
	int text_shndx;
	int symbols_shndx;
	bool has_st_ops;
	int arena_data_shndx;
};

struct usdt_manager;

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
	bool has_subcalls;
	bool has_rodata;

	struct bpf_gen *gen_loader;

	/* Information when doing ELF related work. Only valid if efile.elf is not NULL */
	struct elf_state efile;

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

	/* optional log settings passed to BPF_BTF_LOAD and BPF_PROG_LOAD commands */
	char *log_buf;
	size_t log_size;
	__u32 log_level;

	int *fd_array;
	size_t fd_array_cap;
	size_t fd_array_cnt;

	struct usdt_manager *usdt_man;

	struct bpf_map *arena_map;
	void *arena_data;
	size_t arena_data_sz;

	struct kern_feature_cache *feat_cache;
	char *token_path;
	int token_fd;

	char path[];
};

static const char *elf_sym_str(const struct bpf_object *obj, size_t off);
static const char *elf_sec_str(const struct bpf_object *obj, size_t off);
static Elf_Scn *elf_sec_by_idx(const struct bpf_object *obj, size_t idx);
static Elf_Scn *elf_sec_by_name(const struct bpf_object *obj, const char *name);
static Elf64_Shdr *elf_sec_hdr(const struct bpf_object *obj, Elf_Scn *scn);
static const char *elf_sec_name(const struct bpf_object *obj, Elf_Scn *scn);
static Elf_Data *elf_sec_data(const struct bpf_object *obj, Elf_Scn *scn);
static Elf64_Sym *elf_sym_by_idx(const struct bpf_object *obj, size_t idx);
static Elf64_Rel *elf_rel_by_idx(Elf_Data *data, size_t idx);

void bpf_program__unload(struct bpf_program *prog)
{
	if (!prog)
		return;

	zclose(prog->fd);

	zfree(&prog->func_info);
	zfree(&prog->line_info);
}

static void bpf_program__exit(struct bpf_program *prog)
{
	if (!prog)
		return;

	bpf_program__unload(prog);
	zfree(&prog->name);
	zfree(&prog->sec_name);
	zfree(&prog->insns);
	zfree(&prog->reloc_desc);

	prog->nr_reloc = 0;
	prog->insns_cnt = 0;
	prog->sec_idx = -1;
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
	prog->fd = -1;
	prog->exception_cb_idx = -1;

	/* libbpf's convention for SEC("?abc...") is that it's just like
	 * SEC("abc...") but the corresponding bpf_program starts out with
	 * autoload set to false.
	 */
	if (sec_name[0] == '?') {
		prog->autoload = false;
		/* from now on forget there was ? in section name */
		sec_name++;
	} else {
		prog->autoload = true;
	}

	prog->autoattach = true;

	/* inherit object's log_level */
	prog->log_level = obj->log_level;

	prog->sec_name = strdup(sec_name);
	if (!prog->sec_name)
		goto errout;

	prog->name = strdup(name);
	if (!prog->name)
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
	Elf64_Sym *sym;

	progs = obj->programs;
	nr_progs = obj->nr_programs;
	nr_syms = symbols->d_size / sizeof(Elf64_Sym);

	for (i = 0; i < nr_syms; i++) {
		sym = elf_sym_by_idx(obj, i);

		if (sym->st_shndx != sec_idx)
			continue;
		if (ELF64_ST_TYPE(sym->st_info) != STT_FUNC)
			continue;

		prog_sz = sym->st_size;
		sec_off = sym->st_value;

		name = elf_sym_str(obj, sym->st_name);
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

		if (sec_idx != obj->efile.text_shndx && ELF64_ST_BIND(sym->st_info) == STB_LOCAL) {
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

		if (ELF64_ST_BIND(sym->st_info) != STB_LOCAL)
			prog->sym_global = true;

		/* if function is a global/weak symbol, but has restricted
		 * (STV_HIDDEN or STV_INTERNAL) visibility, mark its BTF FUNC
		 * as static to enable more permissive BPF verification mode
		 * with more outside context available to BPF verifier
		 */
		if (prog->sym_global && (ELF64_ST_VISIBILITY(sym->st_other) == STV_HIDDEN
		    || ELF64_ST_VISIBILITY(sym->st_other) == STV_INTERNAL))
			prog->mark_btf_static = true;

		nr_progs++;
		obj->nr_programs = nr_progs;
	}

	return 0;
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

static int find_ksym_btf_id(struct bpf_object *obj, const char *ksym_name,
			    __u16 kind, struct btf **res_btf,
			    struct module_btf **res_mod_btf);

#define STRUCT_OPS_VALUE_PREFIX "bpf_struct_ops_"
static int find_btf_by_prefix_kind(const struct btf *btf, const char *prefix,
				   const char *name, __u32 kind);

static int
find_struct_ops_kern_types(struct bpf_object *obj, const char *tname_raw,
			   struct module_btf **mod_btf,
			   const struct btf_type **type, __u32 *type_id,
			   const struct btf_type **vtype, __u32 *vtype_id,
			   const struct btf_member **data_member)
{
	const struct btf_type *kern_type, *kern_vtype;
	const struct btf_member *kern_data_member;
	struct btf *btf;
	__s32 kern_vtype_id, kern_type_id;
	char tname[256];
	__u32 i;

	snprintf(tname, sizeof(tname), "%.*s",
		 (int)bpf_core_essential_name_len(tname_raw), tname_raw);

	kern_type_id = find_ksym_btf_id(obj, tname, BTF_KIND_STRUCT,
					&btf, mod_btf);
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

static bool is_valid_st_ops_program(struct bpf_object *obj,
				    const struct bpf_program *prog)
{
	int i;

	for (i = 0; i < obj->nr_programs; i++) {
		if (&obj->programs[i] == prog)
			return prog->type == BPF_PROG_TYPE_STRUCT_OPS;
	}

	return false;
}

/* For each struct_ops program P, referenced from some struct_ops map M,
 * enable P.autoload if there are Ms for which M.autocreate is true,
 * disable P.autoload if for all Ms M.autocreate is false.
 * Don't change P.autoload for programs that are not referenced from any maps.
 */
static int bpf_object_adjust_struct_ops_autoload(struct bpf_object *obj)
{
	struct bpf_program *prog, *slot_prog;
	struct bpf_map *map;
	int i, j, k, vlen;

	for (i = 0; i < obj->nr_programs; ++i) {
		int should_load = false;
		int use_cnt = 0;

		prog = &obj->programs[i];
		if (prog->type != BPF_PROG_TYPE_STRUCT_OPS)
			continue;

		for (j = 0; j < obj->nr_maps; ++j) {
			map = &obj->maps[j];
			if (!bpf_map__is_struct_ops(map))
				continue;

			vlen = btf_vlen(map->st_ops->type);
			for (k = 0; k < vlen; ++k) {
				slot_prog = map->st_ops->progs[k];
				if (prog != slot_prog)
					continue;

				use_cnt++;
				if (map->autocreate)
					should_load = true;
			}
		}
		if (use_cnt)
			prog->autoload = should_load;
	}

	return 0;
}

/* Init the map's fields that depend on kern_btf */
static int bpf_map__init_kern_struct_ops(struct bpf_map *map)
{
	const struct btf_member *member, *kern_member, *kern_data_member;
	const struct btf_type *type, *kern_type, *kern_vtype;
	__u32 i, kern_type_id, kern_vtype_id, kern_data_off;
	struct bpf_object *obj = map->obj;
	const struct btf *btf = obj->btf;
	struct bpf_struct_ops *st_ops;
	const struct btf *kern_btf;
	struct module_btf *mod_btf;
	void *data, *kern_data;
	const char *tname;
	int err;

	st_ops = map->st_ops;
	type = st_ops->type;
	tname = st_ops->tname;
	err = find_struct_ops_kern_types(obj, tname, &mod_btf,
					 &kern_type, &kern_type_id,
					 &kern_vtype, &kern_vtype_id,
					 &kern_data_member);
	if (err)
		return err;

	kern_btf = mod_btf ? mod_btf->btf : obj->btf_vmlinux;

	pr_debug("struct_ops init_kern %s: type_id:%u kern_type_id:%u kern_vtype_id:%u\n",
		 map->name, st_ops->type_id, kern_type_id, kern_vtype_id);

	map->mod_btf_fd = mod_btf ? mod_btf->fd : -1;
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
		struct bpf_program *prog;
		__s64 msize, kern_msize;
		__u32 moff, kern_moff;
		__u32 kern_member_idx;
		const char *mname;

		mname = btf__name_by_offset(btf, member->name_off);
		moff = member->offset / 8;
		mdata = data + moff;
		msize = btf__resolve_size(btf, member->type);
		if (msize < 0) {
			pr_warn("struct_ops init_kern %s: failed to resolve the size of member %s\n",
				map->name, mname);
			return msize;
		}

		kern_member = find_member_by_name(kern_btf, kern_type, mname);
		if (!kern_member) {
			if (!libbpf_is_mem_zeroed(mdata, msize)) {
				pr_warn("struct_ops init_kern %s: Cannot find member %s in kernel BTF\n",
					map->name, mname);
				return -ENOTSUP;
			}

			prog = st_ops->progs[i];
			if (prog) {
				/* If we had declaratively set struct_ops callback, we need to
				 * first validate that it's actually a struct_ops program.
				 * And then force its autoload to false, because it doesn't have
				 * a chance of succeeding from POV of the current struct_ops map.
				 * If this program is still referenced somewhere else, though,
				 * then bpf_object_adjust_struct_ops_autoload() will update its
				 * autoload accordingly.
				 */
				if (!is_valid_st_ops_program(obj, prog)) {
					pr_warn("struct_ops init_kern %s: member %s is declaratively assigned a non-struct_ops program\n",
						map->name, mname);
					return -EINVAL;
				}
				prog->autoload = false;
				st_ops->progs[i] = NULL;
			}

			/* Skip all-zero/NULL fields if they are not present in the kernel BTF */
			pr_info("struct_ops %s: member %s not found in kernel, skipping it as it's set to zero\n",
				map->name, mname);
			continue;
		}

		kern_member_idx = kern_member - btf_members(kern_type);
		if (btf_member_bitfield_size(type, i) ||
		    btf_member_bitfield_size(kern_type, kern_member_idx)) {
			pr_warn("struct_ops init_kern %s: bitfield %s is not supported\n",
				map->name, mname);
			return -ENOTSUP;
		}

		kern_moff = kern_member->offset / 8;
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
			/* Update the value from the shadow type */
			prog = *(void **)mdata;
			st_ops->progs[i] = prog;
			if (!prog)
				continue;
			if (!is_valid_st_ops_program(obj, prog)) {
				pr_warn("struct_ops init_kern %s: member %s is not a struct_ops program\n",
					map->name, mname);
				return -ENOTSUP;
			}

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

			if (mod_btf)
				prog->attach_btf_obj_fd = mod_btf->fd;

			/* if we haven't yet processed this BPF program, record proper
			 * attach_btf_id and member_idx
			 */
			if (!prog->attach_btf_id) {
				prog->attach_btf_id = kern_type_id;
				prog->expected_attach_type = kern_member_idx;
			}

			/* struct_ops BPF prog can be re-used between multiple
			 * .struct_ops & .struct_ops.link as long as it's the
			 * same struct_ops struct definition and the same
			 * function pointer field
			 */
			if (prog->attach_btf_id != kern_type_id) {
				pr_warn("struct_ops init_kern %s func ptr %s: invalid reuse of prog %s in sec %s with type %u: attach_btf_id %u != kern_type_id %u\n",
					map->name, mname, prog->name, prog->sec_name, prog->type,
					prog->attach_btf_id, kern_type_id);
				return -EINVAL;
			}
			if (prog->expected_attach_type != kern_member_idx) {
				pr_warn("struct_ops init_kern %s func ptr %s: invalid reuse of prog %s in sec %s with type %u: expected_attach_type %u != kern_member_idx %u\n",
					map->name, mname, prog->name, prog->sec_name, prog->type,
					prog->expected_attach_type, kern_member_idx);
				return -EINVAL;
			}

			st_ops->kern_func_off[i] = kern_data_off + kern_moff;

			pr_debug("struct_ops init_kern %s: func ptr %s is set to prog %s from data(+%u) to kern_data(+%u)\n",
				 map->name, mname, prog->name, moff,
				 kern_moff);

			continue;
		}

		kern_msize = btf__resolve_size(kern_btf, kern_mtype_id);
		if (kern_msize < 0 || msize != kern_msize) {
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

		if (!map->autocreate)
			continue;

		err = bpf_map__init_kern_struct_ops(map);
		if (err)
			return err;
	}

	return 0;
}

static int init_struct_ops_maps(struct bpf_object *obj, const char *sec_name,
				int shndx, Elf_Data *data)
{
	const struct btf_type *type, *datasec;
	const struct btf_var_secinfo *vsi;
	struct bpf_struct_ops *st_ops;
	const char *tname, *var_name;
	__s32 type_id, datasec_id;
	const struct btf *btf;
	struct bpf_map *map;
	__u32 i;

	if (shndx == -1)
		return 0;

	btf = obj->btf;
	datasec_id = btf__find_by_name_kind(btf, sec_name,
					    BTF_KIND_DATASEC);
	if (datasec_id < 0) {
		pr_warn("struct_ops init: DATASEC %s not found\n",
			sec_name);
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
				vsi->type, sec_name);
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

		map->sec_idx = shndx;
		map->sec_offset = vsi->offset;
		map->name = strdup(var_name);
		if (!map->name)
			return -ENOMEM;
		map->btf_value_type_id = type_id;

		/* Follow same convention as for programs autoload:
		 * SEC("?.struct_ops") means map is not created by default.
		 */
		if (sec_name[0] == '?') {
			map->autocreate = false;
			/* from now on forget there was ? in section name */
			sec_name++;
		}

		map->def.type = BPF_MAP_TYPE_STRUCT_OPS;
		map->def.key_size = sizeof(int);
		map->def.value_size = type->size;
		map->def.max_entries = 1;
		map->def.map_flags = strcmp(sec_name, STRUCT_OPS_LINK_SEC) == 0 ? BPF_F_LINK : 0;

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

		if (vsi->offset + type->size > data->d_size) {
			pr_warn("struct_ops init: var %s is beyond the end of DATASEC %s\n",
				var_name, sec_name);
			return -EINVAL;
		}

		memcpy(st_ops->data,
		       data->d_buf + vsi->offset,
		       type->size);
		st_ops->tname = tname;
		st_ops->type = type;
		st_ops->type_id = type_id;

		pr_debug("struct_ops init: struct %s(type_id=%u) %s found at offset %u\n",
			 tname, type_id, var_name, vsi->offset);
	}

	return 0;
}

static int bpf_object_init_struct_ops(struct bpf_object *obj)
{
	const char *sec_name;
	int sec_idx, err;

	for (sec_idx = 0; sec_idx < obj->efile.sec_cnt; ++sec_idx) {
		struct elf_sec_desc *desc = &obj->efile.secs[sec_idx];

		if (desc->sec_type != SEC_ST_OPS)
			continue;

		sec_name = elf_sec_name(obj, elf_sec_by_idx(obj, sec_idx));
		if (!sec_name)
			return -LIBBPF_ERRNO__FORMAT;

		err = init_struct_ops_maps(obj, sec_name, sec_idx, desc->data);
		if (err)
			return err;
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
		libbpf_strlcpy(obj->name, obj_name, sizeof(obj->name));
	} else {
		/* Using basename() GNU version which doesn't modify arg. */
		libbpf_strlcpy(obj->name, basename((void *)path), sizeof(obj->name));
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
	obj->efile.btf_maps_shndx = -1;
	obj->kconfig_map_idx = -1;

	obj->kern_version = get_kernel_version();
	obj->loaded = false;

	return obj;
}

static void bpf_object__elf_finish(struct bpf_object *obj)
{
	if (!obj->efile.elf)
		return;

	elf_end(obj->efile.elf);
	obj->efile.elf = NULL;
	obj->efile.symbols = NULL;
	obj->efile.arena_data = NULL;

	zfree(&obj->efile.secs);
	obj->efile.sec_cnt = 0;
	zclose(obj->efile.fd);
	obj->efile.obj_buf = NULL;
	obj->efile.obj_buf_sz = 0;
}

static int bpf_object__elf_init(struct bpf_object *obj)
{
	Elf64_Ehdr *ehdr;
	int err = 0;
	Elf *elf;

	if (obj->efile.elf) {
		pr_warn("elf: init internal error\n");
		return -LIBBPF_ERRNO__LIBELF;
	}

	if (obj->efile.obj_buf_sz > 0) {
		/* obj_buf should have been validated by bpf_object__open_mem(). */
		elf = elf_memory((char *)obj->efile.obj_buf, obj->efile.obj_buf_sz);
	} else {
		obj->efile.fd = open(obj->path, O_RDONLY | O_CLOEXEC);
		if (obj->efile.fd < 0) {
			char errmsg[STRERR_BUFSIZE], *cp;

			err = -errno;
			cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
			pr_warn("elf: failed to open %s: %s\n", obj->path, cp);
			return err;
		}

		elf = elf_begin(obj->efile.fd, ELF_C_READ_MMAP, NULL);
	}

	if (!elf) {
		pr_warn("elf: failed to open %s as ELF file: %s\n", obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__LIBELF;
		goto errout;
	}

	obj->efile.elf = elf;

	if (elf_kind(elf) != ELF_K_ELF) {
		err = -LIBBPF_ERRNO__FORMAT;
		pr_warn("elf: '%s' is not a proper ELF object\n", obj->path);
		goto errout;
	}

	if (gelf_getclass(elf) != ELFCLASS64) {
		err = -LIBBPF_ERRNO__FORMAT;
		pr_warn("elf: '%s' is not a 64-bit ELF object\n", obj->path);
		goto errout;
	}

	obj->efile.ehdr = ehdr = elf64_getehdr(elf);
	if (!obj->efile.ehdr) {
		pr_warn("elf: failed to get ELF header from %s: %s\n", obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}

	if (elf_getshdrstrndx(elf, &obj->efile.shstrndx)) {
		pr_warn("elf: failed to get section names section index for %s: %s\n",
			obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}

	/* ELF is corrupted/truncated, avoid calling elf_strptr. */
	if (!elf_rawdata(elf_getscn(elf, obj->efile.shstrndx), NULL)) {
		pr_warn("elf: failed to get section names strings from %s: %s\n",
			obj->path, elf_errmsg(-1));
		err = -LIBBPF_ERRNO__FORMAT;
		goto errout;
	}

	/* Old LLVM set e_machine to EM_NONE */
	if (ehdr->e_type != ET_REL || (ehdr->e_machine && ehdr->e_machine != EM_BPF)) {
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
#if __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	if (obj->efile.ehdr->e_ident[EI_DATA] == ELFDATA2LSB)
		return 0;
#elif __BYTE_ORDER__ == __ORDER_BIG_ENDIAN__
	if (obj->efile.ehdr->e_ident[EI_DATA] == ELFDATA2MSB)
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
	if (!data) {
		pr_warn("invalid license section in %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}
	/* libbpf_strlcpy() only copies first N - 1 bytes, so size + 1 won't
	 * go over allowed ELF data section buffer
	 */
	libbpf_strlcpy(obj->license, data, min(size + 1, sizeof(obj->license)));
	pr_debug("license of %s is %s\n", obj->path, obj->license);
	return 0;
}

static int
bpf_object__init_kversion(struct bpf_object *obj, void *data, size_t size)
{
	__u32 kver;

	if (!data || size != sizeof(kver)) {
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

static int find_elf_sec_sz(const struct bpf_object *obj, const char *name, __u32 *size)
{
	Elf_Data *data;
	Elf_Scn *scn;

	if (!name)
		return -EINVAL;

	scn = elf_sec_by_name(obj, name);
	data = elf_sec_data(obj, scn);
	if (data) {
		*size = data->d_size;
		return 0; /* found it */
	}

	return -ENOENT;
}

static Elf64_Sym *find_elf_var_sym(const struct bpf_object *obj, const char *name)
{
	Elf_Data *symbols = obj->efile.symbols;
	const char *sname;
	size_t si;

	for (si = 0; si < symbols->d_size / sizeof(Elf64_Sym); si++) {
		Elf64_Sym *sym = elf_sym_by_idx(obj, si);

		if (ELF64_ST_TYPE(sym->st_info) != STT_OBJECT)
			continue;

		if (ELF64_ST_BIND(sym->st_info) != STB_GLOBAL &&
		    ELF64_ST_BIND(sym->st_info) != STB_WEAK)
			continue;

		sname = elf_sym_str(obj, sym->st_name);
		if (!sname) {
			pr_warn("failed to get sym name string for var %s\n", name);
			return ERR_PTR(-EIO);
		}
		if (strcmp(name, sname) == 0)
			return sym;
	}

	return ERR_PTR(-ENOENT);
}

/* Some versions of Android don't provide memfd_create() in their libc
 * implementation, so avoid complications and just go straight to Linux
 * syscall.
 */
static int sys_memfd_create(const char *name, unsigned flags)
{
	return syscall(__NR_memfd_create, name, flags);
}

#ifndef MFD_CLOEXEC
#define MFD_CLOEXEC 0x0001U
#endif

static int create_placeholder_fd(void)
{
	int fd;

	fd = ensure_good_fd(sys_memfd_create("libbpf-placeholder-fd", MFD_CLOEXEC));
	if (fd < 0)
		return -errno;
	return fd;
}

static struct bpf_map *bpf_object__add_map(struct bpf_object *obj)
{
	struct bpf_map *map;
	int err;

	err = libbpf_ensure_mem((void **)&obj->maps, &obj->maps_cap,
				sizeof(*obj->maps), obj->nr_maps + 1);
	if (err)
		return ERR_PTR(err);

	map = &obj->maps[obj->nr_maps++];
	map->obj = obj;
	/* Preallocate map FD without actually creating BPF map just yet.
	 * These map FD "placeholders" will be reused later without changing
	 * FD value when map is actually created in the kernel.
	 *
	 * This is useful to be able to perform BPF program relocations
	 * without having to create BPF maps before that step. This allows us
	 * to finalize and load BTF very late in BPF object's loading phase,
	 * right before BPF maps have to be created and BPF programs have to
	 * be loaded. By having these map FD placeholders we can perform all
	 * the sanitizations, relocations, and any other adjustments before we
	 * start creating actual BPF kernel objects (BTF, maps, progs).
	 */
	map->fd = create_placeholder_fd();
	if (map->fd < 0)
		return ERR_PTR(map->fd);
	map->inner_map_fd = -1;
	map->autocreate = true;

	return map;
}

static size_t array_map_mmap_sz(unsigned int value_sz, unsigned int max_entries)
{
	const long page_sz = sysconf(_SC_PAGE_SIZE);
	size_t map_sz;

	map_sz = (size_t)roundup(value_sz, 8) * max_entries;
	map_sz = roundup(map_sz, page_sz);
	return map_sz;
}

static size_t bpf_map_mmap_sz(const struct bpf_map *map)
{
	const long page_sz = sysconf(_SC_PAGE_SIZE);

	switch (map->def.type) {
	case BPF_MAP_TYPE_ARRAY:
		return array_map_mmap_sz(map->def.value_size, map->def.max_entries);
	case BPF_MAP_TYPE_ARENA:
		return page_sz * map->def.max_entries;
	default:
		return 0; /* not supported */
	}
}

static int bpf_map_mmap_resize(struct bpf_map *map, size_t old_sz, size_t new_sz)
{
	void *mmaped;

	if (!map->mmaped)
		return -EINVAL;

	if (old_sz == new_sz)
		return 0;

	mmaped = mmap(NULL, new_sz, PROT_READ | PROT_WRITE, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (mmaped == MAP_FAILED)
		return -errno;

	memcpy(mmaped, map->mmaped, min(old_sz, new_sz));
	munmap(map->mmaped, old_sz);
	map->mmaped = mmaped;
	return 0;
}

static char *internal_map_name(struct bpf_object *obj, const char *real_name)
{
	char map_name[BPF_OBJ_NAME_LEN], *p;
	int pfx_len, sfx_len = max((size_t)7, strlen(real_name));

	/* This is one of the more confusing parts of libbpf for various
	 * reasons, some of which are historical. The original idea for naming
	 * internal names was to include as much of BPF object name prefix as
	 * possible, so that it can be distinguished from similar internal
	 * maps of a different BPF object.
	 * As an example, let's say we have bpf_object named 'my_object_name'
	 * and internal map corresponding to '.rodata' ELF section. The final
	 * map name advertised to user and to the kernel will be
	 * 'my_objec.rodata', taking first 8 characters of object name and
	 * entire 7 characters of '.rodata'.
	 * Somewhat confusingly, if internal map ELF section name is shorter
	 * than 7 characters, e.g., '.bss', we still reserve 7 characters
	 * for the suffix, even though we only have 4 actual characters, and
	 * resulting map will be called 'my_objec.bss', not even using all 15
	 * characters allowed by the kernel. Oh well, at least the truncated
	 * object name is somewhat consistent in this case. But if the map
	 * name is '.kconfig', we'll still have entirety of '.kconfig' added
	 * (8 chars) and thus will be left with only first 7 characters of the
	 * object name ('my_obje'). Happy guessing, user, that the final map
	 * name will be "my_obje.kconfig".
	 * Now, with libbpf starting to support arbitrarily named .rodata.*
	 * and .data.* data sections, it's possible that ELF section name is
	 * longer than allowed 15 chars, so we now need to be careful to take
	 * only up to 15 first characters of ELF name, taking no BPF object
	 * name characters at all. So '.rodata.abracadabra' will result in
	 * '.rodata.abracad' kernel and user-visible name.
	 * We need to keep this convoluted logic intact for .data, .bss and
	 * .rodata maps, but for new custom .data.custom and .rodata.custom
	 * maps we use their ELF names as is, not prepending bpf_object name
	 * in front. We still need to truncate them to 15 characters for the
	 * kernel. Full name can be recovered for such maps by using DATASEC
	 * BTF type associated with such map's value type, though.
	 */
	if (sfx_len >= BPF_OBJ_NAME_LEN)
		sfx_len = BPF_OBJ_NAME_LEN - 1;

	/* if there are two or more dots in map name, it's a custom dot map */
	if (strchr(real_name + 1, '.') != NULL)
		pfx_len = 0;
	else
		pfx_len = min((size_t)BPF_OBJ_NAME_LEN - sfx_len - 1, strlen(obj->name));

	snprintf(map_name, sizeof(map_name), "%.*s%.*s", pfx_len, obj->name,
		 sfx_len, real_name);

	/* sanitise map name to characters allowed by kernel */
	for (p = map_name; *p && p < map_name + sizeof(map_name); p++)
		if (!isalnum(*p) && *p != '_' && *p != '.')
			*p = '_';

	return strdup(map_name);
}

static int
map_fill_btf_type_info(struct bpf_object *obj, struct bpf_map *map);

/* Internal BPF map is mmap()'able only if at least one of corresponding
 * DATASEC's VARs are to be exposed through BPF skeleton. I.e., it's a GLOBAL
 * variable and it's not marked as __hidden (which turns it into, effectively,
 * a STATIC variable).
 */
static bool map_is_mmapable(struct bpf_object *obj, struct bpf_map *map)
{
	const struct btf_type *t, *vt;
	struct btf_var_secinfo *vsi;
	int i, n;

	if (!map->btf_value_type_id)
		return false;

	t = btf__type_by_id(obj->btf, map->btf_value_type_id);
	if (!btf_is_datasec(t))
		return false;

	vsi = btf_var_secinfos(t);
	for (i = 0, n = btf_vlen(t); i < n; i++, vsi++) {
		vt = btf__type_by_id(obj->btf, vsi->type);
		if (!btf_is_var(vt))
			continue;

		if (btf_var(vt)->linkage != BTF_VAR_STATIC)
			return true;
	}

	return false;
}

static int
bpf_object__init_internal_map(struct bpf_object *obj, enum libbpf_map_type type,
			      const char *real_name, int sec_idx, void *data, size_t data_sz)
{
	struct bpf_map_def *def;
	struct bpf_map *map;
	size_t mmap_sz;
	int err;

	map = bpf_object__add_map(obj);
	if (IS_ERR(map))
		return PTR_ERR(map);

	map->libbpf_type = type;
	map->sec_idx = sec_idx;
	map->sec_offset = 0;
	map->real_name = strdup(real_name);
	map->name = internal_map_name(obj, real_name);
	if (!map->real_name || !map->name) {
		zfree(&map->real_name);
		zfree(&map->name);
		return -ENOMEM;
	}

	def = &map->def;
	def->type = BPF_MAP_TYPE_ARRAY;
	def->key_size = sizeof(int);
	def->value_size = data_sz;
	def->max_entries = 1;
	def->map_flags = type == LIBBPF_MAP_RODATA || type == LIBBPF_MAP_KCONFIG
		? BPF_F_RDONLY_PROG : 0;

	/* failures are fine because of maps like .rodata.str1.1 */
	(void) map_fill_btf_type_info(obj, map);

	if (map_is_mmapable(obj, map))
		def->map_flags |= BPF_F_MMAPABLE;

	pr_debug("map '%s' (global data): at sec_idx %d, offset %zu, flags %x.\n",
		 map->name, map->sec_idx, map->sec_offset, def->map_flags);

	mmap_sz = bpf_map_mmap_sz(map);
	map->mmaped = mmap(NULL, mmap_sz, PROT_READ | PROT_WRITE,
			   MAP_SHARED | MAP_ANONYMOUS, -1, 0);
	if (map->mmaped == MAP_FAILED) {
		err = -errno;
		map->mmaped = NULL;
		pr_warn("failed to alloc map '%s' content buffer: %d\n",
			map->name, err);
		zfree(&map->real_name);
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
	struct elf_sec_desc *sec_desc;
	const char *sec_name;
	int err = 0, sec_idx;

	/*
	 * Populate obj->maps with libbpf internal maps.
	 */
	for (sec_idx = 1; sec_idx < obj->efile.sec_cnt; sec_idx++) {
		sec_desc = &obj->efile.secs[sec_idx];

		/* Skip recognized sections with size 0. */
		if (!sec_desc->data || sec_desc->data->d_size == 0)
			continue;

		switch (sec_desc->sec_type) {
		case SEC_DATA:
			sec_name = elf_sec_name(obj, elf_sec_by_idx(obj, sec_idx));
			err = bpf_object__init_internal_map(obj, LIBBPF_MAP_DATA,
							    sec_name, sec_idx,
							    sec_desc->data->d_buf,
							    sec_desc->data->d_size);
			break;
		case SEC_RODATA:
			obj->has_rodata = true;
			sec_name = elf_sec_name(obj, elf_sec_by_idx(obj, sec_idx));
			err = bpf_object__init_internal_map(obj, LIBBPF_MAP_RODATA,
							    sec_name, sec_idx,
							    sec_desc->data->d_buf,
							    sec_desc->data->d_size);
			break;
		case SEC_BSS:
			sec_name = elf_sec_name(obj, elf_sec_by_idx(obj, sec_idx));
			err = bpf_object__init_internal_map(obj, LIBBPF_MAP_BSS,
							    sec_name, sec_idx,
							    NULL,
							    sec_desc->data->d_size);
			break;
		default:
			/* skip */
			break;
		}
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

static struct extern_desc *find_extern_by_name_with_len(const struct bpf_object *obj,
							const void *name, int len)
{
	const char *ext_name;
	int i;

	for (i = 0; i < obj->nr_extern; i++) {
		ext_name = obj->externs[i].name;
		if (strlen(ext_name) == len && strncmp(ext_name, name, len) == 0)
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
			pr_warn("extern (kcfg) '%s': value '%c' implies tristate or char type\n",
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
		pr_warn("extern (kcfg) '%s': value '%c' implies bool, tristate, or char type\n",
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
		pr_warn("extern (kcfg) '%s': value '%s' implies char array type\n",
			ext->name, value);
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
		pr_warn("extern (kcfg) '%s': long string '%s' of (%zu bytes) truncated to %d bytes\n",
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
	if (ext->kcfg.type != KCFG_INT && ext->kcfg.type != KCFG_CHAR &&
	    ext->kcfg.type != KCFG_BOOL) {
		pr_warn("extern (kcfg) '%s': value '%llu' implies integer, char, or boolean type\n",
			ext->name, (unsigned long long)value);
		return -EINVAL;
	}
	if (ext->kcfg.type == KCFG_BOOL && value > 1) {
		pr_warn("extern (kcfg) '%s': value '%llu' isn't boolean compatible\n",
			ext->name, (unsigned long long)value);
		return -EINVAL;

	}
	if (!is_kcfg_value_in_range(ext, value)) {
		pr_warn("extern (kcfg) '%s': value '%llu' doesn't fit in %d bytes\n",
			ext->name, (unsigned long long)value, ext->kcfg.sz);
		return -ERANGE;
	}
	switch (ext->kcfg.sz) {
	case 1:
		*(__u8 *)ext_val = value;
		break;
	case 2:
		*(__u16 *)ext_val = value;
		break;
	case 4:
		*(__u32 *)ext_val = value;
		break;
	case 8:
		*(__u64 *)ext_val = value;
		break;
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

	if (!str_has_pfx(buf, "CONFIG_"))
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
			pr_warn("extern (kcfg) '%s': value '%s' isn't a valid integer\n", ext->name, value);
			return err;
		}
		if (ext->kcfg.type != KCFG_INT && ext->kcfg.type != KCFG_CHAR) {
			pr_warn("extern (kcfg) '%s': value '%s' implies integer type\n", ext->name, value);
			return -EINVAL;
		}
		err = set_kcfg_value_num(ext, ext_val, num);
		break;
	}
	if (err)
		return err;
	pr_debug("extern (kcfg) '%s': set to %s\n", ext->name, value);
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
	file = gzopen(buf, "re");
	if (!file)
		file = gzopen("/proc/config.gz", "re");

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
					    ".kconfig", obj->efile.symbols_shndx,
					    NULL, map_sz);
	if (err)
		return err;

	obj->kconfig_map_idx = obj->nr_maps - 1;

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
	case BTF_KIND_DECL_TAG: return "decl_tag";
	case BTF_KIND_TYPE_TAG: return "type_tag";
	case BTF_KIND_ENUM64: return "enum64";
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

static bool get_map_field_long(const char *map_name, const struct btf *btf,
			       const struct btf_member *m, __u64 *res)
{
	const struct btf_type *t = skip_mods_and_typedefs(btf, m->type, NULL);
	const char *name = btf__name_by_offset(btf, m->name_off);

	if (btf_is_ptr(t)) {
		__u32 res32;
		bool ret;

		ret = get_map_field_int(map_name, btf, m, &res32);
		if (ret)
			*res = (__u64)res32;
		return ret;
	}

	if (!btf_is_enum(t) && !btf_is_enum64(t)) {
		pr_warn("map '%s': attr '%s': expected ENUM or ENUM64, got %s.\n",
			map_name, name, btf_kind_str(t));
		return false;
	}

	if (btf_vlen(t) != 1) {
		pr_warn("map '%s': attr '%s': invalid __ulong\n",
			map_name, name);
		return false;
	}

	if (btf_is_enum(t)) {
		const struct btf_enum *e = btf_enum(t);

		*res = e->val;
	} else {
		const struct btf_enum64 *e = btf_enum64(t);

		*res = btf_enum64_value(e);
	}
	return true;
}

static int pathname_concat(char *buf, size_t buf_sz, const char *path, const char *name)
{
	int len;

	len = snprintf(buf, buf_sz, "%s/%s", path, name);
	if (len < 0)
		return -EINVAL;
	if (len >= buf_sz)
		return -ENAMETOOLONG;

	return 0;
}

static int build_map_pin_path(struct bpf_map *map, const char *path)
{
	char buf[PATH_MAX];
	int err;

	if (!path)
		path = BPF_FS_DEFAULT_PATH;

	err = pathname_concat(buf, sizeof(buf), path, bpf_map__name(map));
	if (err)
		return err;

	return bpf_map__set_pin_path(map, buf);
}

/* should match definition in bpf_helpers.h */
enum libbpf_pin_type {
	LIBBPF_PIN_NONE,
	/* PIN_BY_NAME: pin maps by name (in /sys/fs/bpf by default) */
	LIBBPF_PIN_BY_NAME,
};

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
			bool is_map_in_map = bpf_map_type__is_map_in_map(map_def->map_type);
			bool is_prog_array = map_def->map_type == BPF_MAP_TYPE_PROG_ARRAY;
			const char *desc = is_map_in_map ? "map-in-map inner" : "prog-array value";
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
			if (!is_map_in_map && !is_prog_array) {
				pr_warn("map '%s': should be map-in-map or prog-array.\n",
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
				pr_warn("map '%s': %s type [%d] not found.\n",
					map_name, desc, m->type);
				return -EINVAL;
			}
			if (!btf_is_array(t) || btf_array(t)->nelems) {
				pr_warn("map '%s': %s spec is not a zero-sized array.\n",
					map_name, desc);
				return -EINVAL;
			}
			t = skip_mods_and_typedefs(btf, btf_array(t)->type, NULL);
			if (!btf_is_ptr(t)) {
				pr_warn("map '%s': %s def is of unexpected kind %s.\n",
					map_name, desc, btf_kind_str(t));
				return -EINVAL;
			}
			t = skip_mods_and_typedefs(btf, t->type, NULL);
			if (is_prog_array) {
				if (!btf_is_func_proto(t)) {
					pr_warn("map '%s': prog-array value def is of unexpected kind %s.\n",
						map_name, btf_kind_str(t));
					return -EINVAL;
				}
				continue;
			}
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
		} else if (strcmp(name, "map_extra") == 0) {
			__u64 map_extra;

			if (!get_map_field_long(map_name, btf, m, &map_extra))
				return -EINVAL;
			map_def->map_extra = map_extra;
			map_def->parts |= MAP_DEF_MAP_EXTRA;
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

static size_t adjust_ringbuf_sz(size_t sz)
{
	__u32 page_sz = sysconf(_SC_PAGE_SIZE);
	__u32 mul;

	/* if user forgot to set any size, make sure they see error */
	if (sz == 0)
		return 0;
	/* Kernel expects BPF_MAP_TYPE_RINGBUF's max_entries to be
	 * a power-of-2 multiple of kernel's page size. If user diligently
	 * satisified these conditions, pass the size through.
	 */
	if ((sz % page_sz) == 0 && is_pow_of_2(sz / page_sz))
		return sz;

	/* Otherwise find closest (page_sz * power_of_2) product bigger than
	 * user-set size to satisfy both user size request and kernel
	 * requirements and substitute correct max_entries for map creation.
	 */
	for (mul = 1; mul <= UINT_MAX / page_sz; mul <<= 1) {
		if (mul * page_sz > sz)
			return mul * page_sz;
	}

	/* if it's impossible to satisfy the conditions (i.e., user size is
	 * very close to UINT_MAX but is not a power-of-2 multiple of
	 * page_size) then just return original size and let kernel reject it
	 */
	return sz;
}

static bool map_is_ringbuf(const struct bpf_map *map)
{
	return map->def.type == BPF_MAP_TYPE_RINGBUF ||
	       map->def.type == BPF_MAP_TYPE_USER_RINGBUF;
}

static void fill_map_from_def(struct bpf_map *map, const struct btf_map_def *def)
{
	map->def.type = def->map_type;
	map->def.key_size = def->key_size;
	map->def.value_size = def->value_size;
	map->def.max_entries = def->max_entries;
	map->def.map_flags = def->map_flags;
	map->map_extra = def->map_extra;

	map->numa_node = def->numa_node;
	map->btf_key_type_id = def->key_type_id;
	map->btf_value_type_id = def->value_type_id;

	/* auto-adjust BPF ringbuf map max_entries to be a multiple of page size */
	if (map_is_ringbuf(map))
		map->def.max_entries = adjust_ringbuf_sz(map->def.max_entries);

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
		pr_debug("map '%s': found map_flags = 0x%x.\n", map->name, def->map_flags);
	if (def->parts & MAP_DEF_MAP_EXTRA)
		pr_debug("map '%s': found map_extra = 0x%llx.\n", map->name,
			 (unsigned long long)def->map_extra);
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
		map->inner_map->fd = create_placeholder_fd();
		if (map->inner_map->fd < 0)
			return map->inner_map->fd;
		map->inner_map->sec_idx = sec_idx;
		map->inner_map->name = malloc(strlen(map_name) + sizeof(".inner") + 1);
		if (!map->inner_map->name)
			return -ENOMEM;
		sprintf(map->inner_map->name, "%s.inner", map_name);

		fill_map_from_def(map->inner_map, &inner_def);
	}

	err = map_fill_btf_type_info(obj, map);
	if (err)
		return err;

	return 0;
}

static int init_arena_map_data(struct bpf_object *obj, struct bpf_map *map,
			       const char *sec_name, int sec_idx,
			       void *data, size_t data_sz)
{
	const long page_sz = sysconf(_SC_PAGE_SIZE);
	size_t mmap_sz;

	mmap_sz = bpf_map_mmap_sz(obj->arena_map);
	if (roundup(data_sz, page_sz) > mmap_sz) {
		pr_warn("elf: sec '%s': declared ARENA map size (%zu) is too small to hold global __arena variables of size %zu\n",
			sec_name, mmap_sz, data_sz);
		return -E2BIG;
	}

	obj->arena_data = malloc(data_sz);
	if (!obj->arena_data)
		return -ENOMEM;
	memcpy(obj->arena_data, data, data_sz);
	obj->arena_data_sz = data_sz;

	/* make bpf_map__init_value() work for ARENA maps */
	map->mmaped = obj->arena_data;

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

	nr_types = btf__type_cnt(obj->btf);
	for (i = 1; i < nr_types; i++) {
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

	for (i = 0; i < obj->nr_maps; i++) {
		struct bpf_map *map = &obj->maps[i];

		if (map->def.type != BPF_MAP_TYPE_ARENA)
			continue;

		if (obj->arena_map) {
			pr_warn("map '%s': only single ARENA map is supported (map '%s' is also ARENA)\n",
				map->name, obj->arena_map->name);
			return -EINVAL;
		}
		obj->arena_map = map;

		if (obj->efile.arena_data) {
			err = init_arena_map_data(obj, map, ARENA_SEC, obj->efile.arena_data_shndx,
						  obj->efile.arena_data->d_buf,
						  obj->efile.arena_data->d_size);
			if (err)
				return err;
		}
	}
	if (obj->efile.arena_data && !obj->arena_map) {
		pr_warn("elf: sec '%s': to use global __arena variables the ARENA map should be explicitly declared in SEC(\".maps\")\n",
			ARENA_SEC);
		return -ENOENT;
	}

	return 0;
}

static int bpf_object__init_maps(struct bpf_object *obj,
				 const struct bpf_object_open_opts *opts)
{
	const char *pin_root_path;
	bool strict;
	int err = 0;

	strict = !OPTS_GET(opts, relaxed_maps, false);
	pin_root_path = OPTS_GET(opts, pin_root_path, NULL);

	err = bpf_object__init_user_btf_maps(obj, strict, pin_root_path);
	err = err ?: bpf_object__init_global_data_maps(obj);
	err = err ?: bpf_object__init_kconfig_map(obj);
	err = err ?: bpf_object_init_struct_ops(obj);

	return err;
}

static bool section_have_execinstr(struct bpf_object *obj, int idx)
{
	Elf64_Shdr *sh;

	sh = elf_sec_hdr(obj, elf_sec_by_idx(obj, idx));
	if (!sh)
		return false;

	return sh->sh_flags & SHF_EXECINSTR;
}

static bool starts_with_qmark(const char *s)
{
	return s && s[0] == '?';
}

static bool btf_needs_sanitization(struct bpf_object *obj)
{
	bool has_func_global = kernel_supports(obj, FEAT_BTF_GLOBAL_FUNC);
	bool has_datasec = kernel_supports(obj, FEAT_BTF_DATASEC);
	bool has_float = kernel_supports(obj, FEAT_BTF_FLOAT);
	bool has_func = kernel_supports(obj, FEAT_BTF_FUNC);
	bool has_decl_tag = kernel_supports(obj, FEAT_BTF_DECL_TAG);
	bool has_type_tag = kernel_supports(obj, FEAT_BTF_TYPE_TAG);
	bool has_enum64 = kernel_supports(obj, FEAT_BTF_ENUM64);
	bool has_qmark_datasec = kernel_supports(obj, FEAT_BTF_QMARK_DATASEC);

	return !has_func || !has_datasec || !has_func_global || !has_float ||
	       !has_decl_tag || !has_type_tag || !has_enum64 || !has_qmark_datasec;
}

static int bpf_object__sanitize_btf(struct bpf_object *obj, struct btf *btf)
{
	bool has_func_global = kernel_supports(obj, FEAT_BTF_GLOBAL_FUNC);
	bool has_datasec = kernel_supports(obj, FEAT_BTF_DATASEC);
	bool has_float = kernel_supports(obj, FEAT_BTF_FLOAT);
	bool has_func = kernel_supports(obj, FEAT_BTF_FUNC);
	bool has_decl_tag = kernel_supports(obj, FEAT_BTF_DECL_TAG);
	bool has_type_tag = kernel_supports(obj, FEAT_BTF_TYPE_TAG);
	bool has_enum64 = kernel_supports(obj, FEAT_BTF_ENUM64);
	bool has_qmark_datasec = kernel_supports(obj, FEAT_BTF_QMARK_DATASEC);
	int enum64_placeholder_id = 0;
	struct btf_type *t;
	int i, j, vlen;

	for (i = 1; i < btf__type_cnt(btf); i++) {
		t = (struct btf_type *)btf__type_by_id(btf, i);

		if ((!has_datasec && btf_is_var(t)) || (!has_decl_tag && btf_is_decl_tag(t))) {
			/* replace VAR/DECL_TAG with INT */
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
				if (*name == '.' || *name == '?')
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
		} else if (!has_qmark_datasec && btf_is_datasec(t) &&
			   starts_with_qmark(btf__name_by_offset(btf, t->name_off))) {
			/* replace '?' prefix with '_' for DATASEC names */
			char *name;

			name = (char *)btf__name_by_offset(btf, t->name_off);
			if (name[0] == '?')
				name[0] = '_';
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
		} else if (!has_type_tag && btf_is_type_tag(t)) {
			/* replace TYPE_TAG with a CONST */
			t->name_off = 0;
			t->info = BTF_INFO_ENC(BTF_KIND_CONST, 0, 0);
		} else if (!has_enum64 && btf_is_enum(t)) {
			/* clear the kflag */
			t->info = btf_type_info(btf_kind(t), btf_vlen(t), false);
		} else if (!has_enum64 && btf_is_enum64(t)) {
			/* replace ENUM64 with a union */
			struct btf_member *m;

			if (enum64_placeholder_id == 0) {
				enum64_placeholder_id = btf__add_int(btf, "enum64_placeholder", 1, 0);
				if (enum64_placeholder_id < 0)
					return enum64_placeholder_id;

				t = (struct btf_type *)btf__type_by_id(btf, i);
			}

			m = btf_members(t);
			vlen = btf_vlen(t);
			t->info = BTF_INFO_ENC(BTF_KIND_UNION, 0, vlen);
			for (j = 0; j < vlen; j++, m++) {
				m->type = enum64_placeholder_id;
				m->offset = 0;
			}
		}
	}

	return 0;
}

static bool libbpf_needs_btf(const struct bpf_object *obj)
{
	return obj->efile.btf_maps_shndx >= 0 ||
	       obj->efile.has_st_ops ||
	       obj->nr_extern > 0;
}

static bool kernel_needs_btf(const struct bpf_object *obj)
{
	return obj->efile.has_st_ops;
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
		struct btf_ext_info *ext_segs[3];
		int seg_num, sec_num;

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

		/* setup .BTF.ext to ELF section mapping */
		ext_segs[0] = &obj->btf_ext->func_info;
		ext_segs[1] = &obj->btf_ext->line_info;
		ext_segs[2] = &obj->btf_ext->core_relo_info;
		for (seg_num = 0; seg_num < ARRAY_SIZE(ext_segs); seg_num++) {
			struct btf_ext_info *seg = ext_segs[seg_num];
			const struct btf_ext_info_sec *sec;
			const char *sec_name;
			Elf_Scn *scn;

			if (seg->sec_cnt == 0)
				continue;

			seg->sec_idxs = calloc(seg->sec_cnt, sizeof(*seg->sec_idxs));
			if (!seg->sec_idxs) {
				err = -ENOMEM;
				goto out;
			}

			sec_num = 0;
			for_each_btf_ext_sec(seg, sec) {
				/* preventively increment index to avoid doing
				 * this before every continue below
				 */
				sec_num++;

				sec_name = btf__name_by_offset(obj->btf, sec->sec_name_off);
				if (str_is_empty(sec_name))
					continue;
				scn = elf_sec_by_name(obj, sec_name);
				if (!scn)
					continue;

				seg->sec_idxs[sec_num - 1] = elf_ndxscn(scn);
			}
		}
	}
out:
	if (err && libbpf_needs_btf(obj)) {
		pr_warn("BTF is required, but is missing or corrupted.\n");
		return err;
	}
	return 0;
}

static int compare_vsi_off(const void *_a, const void *_b)
{
	const struct btf_var_secinfo *a = _a;
	const struct btf_var_secinfo *b = _b;

	return a->offset - b->offset;
}

static int btf_fixup_datasec(struct bpf_object *obj, struct btf *btf,
			     struct btf_type *t)
{
	__u32 size = 0, i, vars = btf_vlen(t);
	const char *sec_name = btf__name_by_offset(btf, t->name_off);
	struct btf_var_secinfo *vsi;
	bool fixup_offsets = false;
	int err;

	if (!sec_name) {
		pr_debug("No name found in string section for DATASEC kind.\n");
		return -ENOENT;
	}

	/* Extern-backing datasecs (.ksyms, .kconfig) have their size and
	 * variable offsets set at the previous step. Further, not every
	 * extern BTF VAR has corresponding ELF symbol preserved, so we skip
	 * all fixups altogether for such sections and go straight to sorting
	 * VARs within their DATASEC.
	 */
	if (strcmp(sec_name, KCONFIG_SEC) == 0 || strcmp(sec_name, KSYMS_SEC) == 0)
		goto sort_vars;

	/* Clang leaves DATASEC size and VAR offsets as zeroes, so we need to
	 * fix this up. But BPF static linker already fixes this up and fills
	 * all the sizes and offsets during static linking. So this step has
	 * to be optional. But the STV_HIDDEN handling is non-optional for any
	 * non-extern DATASEC, so the variable fixup loop below handles both
	 * functions at the same time, paying the cost of BTF VAR <-> ELF
	 * symbol matching just once.
	 */
	if (t->size == 0) {
		err = find_elf_sec_sz(obj, sec_name, &size);
		if (err || !size) {
			pr_debug("sec '%s': failed to determine size from ELF: size %u, err %d\n",
				 sec_name, size, err);
			return -ENOENT;
		}

		t->size = size;
		fixup_offsets = true;
	}

	for (i = 0, vsi = btf_var_secinfos(t); i < vars; i++, vsi++) {
		const struct btf_type *t_var;
		struct btf_var *var;
		const char *var_name;
		Elf64_Sym *sym;

		t_var = btf__type_by_id(btf, vsi->type);
		if (!t_var || !btf_is_var(t_var)) {
			pr_debug("sec '%s': unexpected non-VAR type found\n", sec_name);
			return -EINVAL;
		}

		var = btf_var(t_var);
		if (var->linkage == BTF_VAR_STATIC || var->linkage == BTF_VAR_GLOBAL_EXTERN)
			continue;

		var_name = btf__name_by_offset(btf, t_var->name_off);
		if (!var_name) {
			pr_debug("sec '%s': failed to find name of DATASEC's member #%d\n",
				 sec_name, i);
			return -ENOENT;
		}

		sym = find_elf_var_sym(obj, var_name);
		if (IS_ERR(sym)) {
			pr_debug("sec '%s': failed to find ELF symbol for VAR '%s'\n",
				 sec_name, var_name);
			return -ENOENT;
		}

		if (fixup_offsets)
			vsi->offset = sym->st_value;

		/* if variable is a global/weak symbol, but has restricted
		 * (STV_HIDDEN or STV_INTERNAL) visibility, mark its BTF VAR
		 * as static. This follows similar logic for functions (BPF
		 * subprogs) and influences libbpf's further decisions about
		 * whether to make global data BPF array maps as
		 * BPF_F_MMAPABLE.
		 */
		if (ELF64_ST_VISIBILITY(sym->st_other) == STV_HIDDEN
		    || ELF64_ST_VISIBILITY(sym->st_other) == STV_INTERNAL)
			var->linkage = BTF_VAR_STATIC;
	}

sort_vars:
	qsort(btf_var_secinfos(t), vars, sizeof(*vsi), compare_vsi_off);
	return 0;
}

static int bpf_object_fixup_btf(struct bpf_object *obj)
{
	int i, n, err = 0;

	if (!obj->btf)
		return 0;

	n = btf__type_cnt(obj->btf);
	for (i = 1; i < n; i++) {
		struct btf_type *t = btf_type_by_id(obj->btf, i);

		/* Loader needs to fix up some of the things compiler
		 * couldn't get its hands on while emitting BTF. This
		 * is section size and global variable offset. We use
		 * the info from the ELF itself for this purpose.
		 */
		if (btf_is_datasec(t)) {
			err = btf_fixup_datasec(obj, obj->btf, t);
			if (err)
				return err;
		}
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

static bool map_needs_vmlinux_btf(struct bpf_map *map)
{
	return bpf_map__is_struct_ops(map);
}

static bool obj_needs_vmlinux_btf(const struct bpf_object *obj)
{
	struct bpf_program *prog;
	struct bpf_map *map;
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
		if (!prog->autoload)
			continue;
		if (prog_needs_vmlinux_btf(prog))
			return true;
	}

	bpf_object__for_each_map(map, obj) {
		if (map_needs_vmlinux_btf(map))
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

		n = btf__type_cnt(obj->btf);
		for (j = 1; j < n; j++) {
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
		raw_data = btf__raw_data(obj->btf, &sz);
		kern_btf = btf__new(raw_data, sz);
		err = libbpf_get_error(kern_btf);
		if (err)
			return err;

		/* enforce 8-byte pointers for BPF-targeted BTFs */
		btf__set_pointer_size(obj->btf, 8);
		err = bpf_object__sanitize_btf(obj, kern_btf);
		if (err)
			return err;
	}

	if (obj->gen_loader) {
		__u32 raw_size = 0;
		const void *raw_data = btf__raw_data(kern_btf, &raw_size);

		if (!raw_data)
			return -ENOMEM;
		bpf_gen__load_btf(obj->gen_loader, raw_data, raw_size);
		/* Pretend to have valid FD to pass various fd >= 0 checks.
		 * This fd == 0 will not be used with any syscall and will be reset to -1 eventually.
		 */
		btf__set_fd(kern_btf, 0);
	} else {
		/* currently BPF_BTF_LOAD only supports log_level 1 */
		err = btf_load_into_kernel(kern_btf, obj->log_buf, obj->log_size,
					   obj->log_level ? 1 : 0, obj->token_fd);
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

static Elf64_Shdr *elf_sec_hdr(const struct bpf_object *obj, Elf_Scn *scn)
{
	Elf64_Shdr *shdr;

	if (!scn)
		return NULL;

	shdr = elf64_getshdr(scn);
	if (!shdr) {
		pr_warn("elf: failed to get section(%zu) header from %s: %s\n",
			elf_ndxscn(scn), obj->path, elf_errmsg(-1));
		return NULL;
	}

	return shdr;
}

static const char *elf_sec_name(const struct bpf_object *obj, Elf_Scn *scn)
{
	const char *name;
	Elf64_Shdr *sh;

	if (!scn)
		return NULL;

	sh = elf_sec_hdr(obj, scn);
	if (!sh)
		return NULL;

	name = elf_sec_str(obj, sh->sh_name);
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

static Elf64_Sym *elf_sym_by_idx(const struct bpf_object *obj, size_t idx)
{
	if (idx >= obj->efile.symbols->d_size / sizeof(Elf64_Sym))
		return NULL;

	return (Elf64_Sym *)obj->efile.symbols->d_buf + idx;
}

static Elf64_Rel *elf_rel_by_idx(Elf_Data *data, size_t idx)
{
	if (idx >= data->d_size / sizeof(Elf64_Rel))
		return NULL;

	return (Elf64_Rel *)data->d_buf + idx;
}

static bool is_sec_name_dwarf(const char *name)
{
	/* approximation, but the actual list is too long */
	return str_has_pfx(name, ".debug_");
}

static bool ignore_elf_section(Elf64_Shdr *hdr, const char *name)
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

	if (str_has_pfx(name, ".rel")) {
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
	struct elf_sec_desc *sec_desc;
	Elf *elf = obj->efile.elf;
	Elf_Data *btf_ext_data = NULL;
	Elf_Data *btf_data = NULL;
	int idx = 0, err = 0;
	const char *name;
	Elf_Data *data;
	Elf_Scn *scn;
	Elf64_Shdr *sh;

	/* ELF section indices are 0-based, but sec #0 is special "invalid"
	 * section. Since section count retrieved by elf_getshdrnum() does
	 * include sec #0, it is already the necessary size of an array to keep
	 * all the sections.
	 */
	if (elf_getshdrnum(obj->efile.elf, &obj->efile.sec_cnt)) {
		pr_warn("elf: failed to get the number of sections for %s: %s\n",
			obj->path, elf_errmsg(-1));
		return -LIBBPF_ERRNO__FORMAT;
	}
	obj->efile.secs = calloc(obj->efile.sec_cnt, sizeof(*obj->efile.secs));
	if (!obj->efile.secs)
		return -ENOMEM;

	/* a bunch of ELF parsing functionality depends on processing symbols,
	 * so do the first pass and find the symbol table
	 */
	scn = NULL;
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		sh = elf_sec_hdr(obj, scn);
		if (!sh)
			return -LIBBPF_ERRNO__FORMAT;

		if (sh->sh_type == SHT_SYMTAB) {
			if (obj->efile.symbols) {
				pr_warn("elf: multiple symbol tables in %s\n", obj->path);
				return -LIBBPF_ERRNO__FORMAT;
			}

			data = elf_sec_data(obj, scn);
			if (!data)
				return -LIBBPF_ERRNO__FORMAT;

			idx = elf_ndxscn(scn);

			obj->efile.symbols = data;
			obj->efile.symbols_shndx = idx;
			obj->efile.strtabidx = sh->sh_link;
		}
	}

	if (!obj->efile.symbols) {
		pr_warn("elf: couldn't find symbol table in %s, stripped object file?\n",
			obj->path);
		return -ENOENT;
	}

	scn = NULL;
	while ((scn = elf_nextscn(elf, scn)) != NULL) {
		idx = elf_ndxscn(scn);
		sec_desc = &obj->efile.secs[idx];

		sh = elf_sec_hdr(obj, scn);
		if (!sh)
			return -LIBBPF_ERRNO__FORMAT;

		name = elf_sec_str(obj, sh->sh_name);
		if (!name)
			return -LIBBPF_ERRNO__FORMAT;

		if (ignore_elf_section(sh, name))
			continue;

		data = elf_sec_data(obj, scn);
		if (!data)
			return -LIBBPF_ERRNO__FORMAT;

		pr_debug("elf: section(%d) %s, size %ld, link %d, flags %lx, type=%d\n",
			 idx, name, (unsigned long)data->d_size,
			 (int)sh->sh_link, (unsigned long)sh->sh_flags,
			 (int)sh->sh_type);

		if (strcmp(name, "license") == 0) {
			err = bpf_object__init_license(obj, data->d_buf, data->d_size);
			if (err)
				return err;
		} else if (strcmp(name, "version") == 0) {
			err = bpf_object__init_kversion(obj, data->d_buf, data->d_size);
			if (err)
				return err;
		} else if (strcmp(name, "maps") == 0) {
			pr_warn("elf: legacy map definitions in 'maps' section are not supported by libbpf v1.0+\n");
			return -ENOTSUP;
		} else if (strcmp(name, MAPS_ELF_SEC) == 0) {
			obj->efile.btf_maps_shndx = idx;
		} else if (strcmp(name, BTF_ELF_SEC) == 0) {
			if (sh->sh_type != SHT_PROGBITS)
				return -LIBBPF_ERRNO__FORMAT;
			btf_data = data;
		} else if (strcmp(name, BTF_EXT_ELF_SEC) == 0) {
			if (sh->sh_type != SHT_PROGBITS)
				return -LIBBPF_ERRNO__FORMAT;
			btf_ext_data = data;
		} else if (sh->sh_type == SHT_SYMTAB) {
			/* already processed during the first pass above */
		} else if (sh->sh_type == SHT_PROGBITS && data->d_size > 0) {
			if (sh->sh_flags & SHF_EXECINSTR) {
				if (strcmp(name, ".text") == 0)
					obj->efile.text_shndx = idx;
				err = bpf_object__add_programs(obj, data, name, idx);
				if (err)
					return err;
			} else if (strcmp(name, DATA_SEC) == 0 ||
				   str_has_pfx(name, DATA_SEC ".")) {
				sec_desc->sec_type = SEC_DATA;
				sec_desc->shdr = sh;
				sec_desc->data = data;
			} else if (strcmp(name, RODATA_SEC) == 0 ||
				   str_has_pfx(name, RODATA_SEC ".")) {
				sec_desc->sec_type = SEC_RODATA;
				sec_desc->shdr = sh;
				sec_desc->data = data;
			} else if (strcmp(name, STRUCT_OPS_SEC) == 0 ||
				   strcmp(name, STRUCT_OPS_LINK_SEC) == 0 ||
				   strcmp(name, "?" STRUCT_OPS_SEC) == 0 ||
				   strcmp(name, "?" STRUCT_OPS_LINK_SEC) == 0) {
				sec_desc->sec_type = SEC_ST_OPS;
				sec_desc->shdr = sh;
				sec_desc->data = data;
				obj->efile.has_st_ops = true;
			} else if (strcmp(name, ARENA_SEC) == 0) {
				obj->efile.arena_data = data;
				obj->efile.arena_data_shndx = idx;
			} else {
				pr_info("elf: skipping unrecognized data section(%d) %s\n",
					idx, name);
			}
		} else if (sh->sh_type == SHT_REL) {
			int targ_sec_idx = sh->sh_info; /* points to other section */

			if (sh->sh_entsize != sizeof(Elf64_Rel) ||
			    targ_sec_idx >= obj->efile.sec_cnt)
				return -LIBBPF_ERRNO__FORMAT;

			/* Only do relo for section with exec instructions */
			if (!section_have_execinstr(obj, targ_sec_idx) &&
			    strcmp(name, ".rel" STRUCT_OPS_SEC) &&
			    strcmp(name, ".rel" STRUCT_OPS_LINK_SEC) &&
			    strcmp(name, ".rel?" STRUCT_OPS_SEC) &&
			    strcmp(name, ".rel?" STRUCT_OPS_LINK_SEC) &&
			    strcmp(name, ".rel" MAPS_ELF_SEC)) {
				pr_info("elf: skipping relo section(%d) %s for section(%d) %s\n",
					idx, name, targ_sec_idx,
					elf_sec_name(obj, elf_sec_by_idx(obj, targ_sec_idx)) ?: "<?>");
				continue;
			}

			sec_desc->sec_type = SEC_RELO;
			sec_desc->shdr = sh;
			sec_desc->data = data;
		} else if (sh->sh_type == SHT_NOBITS && (strcmp(name, BSS_SEC) == 0 ||
							 str_has_pfx(name, BSS_SEC "."))) {
			sec_desc->sec_type = SEC_BSS;
			sec_desc->shdr = sh;
			sec_desc->data = data;
		} else {
			pr_info("elf: skipping section(%d) %s (size %zu)\n", idx, name,
				(size_t)sh->sh_size);
		}
	}

	if (!obj->efile.strtabidx || obj->efile.strtabidx > idx) {
		pr_warn("elf: symbol strings section missing or invalid in %s\n", obj->path);
		return -LIBBPF_ERRNO__FORMAT;
	}

	/* sort BPF programs by section name and in-section instruction offset
	 * for faster search
	 */
	if (obj->nr_programs)
		qsort(obj->programs, obj->nr_programs, sizeof(*obj->programs), cmp_progs);

	return bpf_object__init_btf(obj, btf_data, btf_ext_data);
}

static bool sym_is_extern(const Elf64_Sym *sym)
{
	int bind = ELF64_ST_BIND(sym->st_info);
	/* externs are symbols w/ type=NOTYPE, bind=GLOBAL|WEAK, section=UND */
	return sym->st_shndx == SHN_UNDEF &&
	       (bind == STB_GLOBAL || bind == STB_WEAK) &&
	       ELF64_ST_TYPE(sym->st_info) == STT_NOTYPE;
}

static bool sym_is_subprog(const Elf64_Sym *sym, int text_shndx)
{
	int bind = ELF64_ST_BIND(sym->st_info);
	int type = ELF64_ST_TYPE(sym->st_info);

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

	n = btf__type_cnt(btf);
	for (i = 1; i < n; i++) {
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

	n = btf__type_cnt(btf);
	for (i = 1; i < n; i++) {
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
	case BTF_KIND_ENUM64:
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

	n = btf__type_cnt(btf);
	for (i = 1; i < n; i++) {
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
	size_t ext_essent_len;
	Elf_Scn *scn;
	Elf64_Shdr *sh;

	if (!obj->efile.symbols)
		return 0;

	scn = elf_sec_by_idx(obj, obj->efile.symbols_shndx);
	sh = elf_sec_hdr(obj, scn);
	if (!sh || sh->sh_entsize != sizeof(Elf64_Sym))
		return -LIBBPF_ERRNO__FORMAT;

	dummy_var_btf_id = add_dummy_ksym_var(obj->btf);
	if (dummy_var_btf_id < 0)
		return dummy_var_btf_id;

	n = sh->sh_size / sh->sh_entsize;
	pr_debug("looking for externs among %d symbols...\n", n);

	for (i = 0; i < n; i++) {
		Elf64_Sym *sym = elf_sym_by_idx(obj, i);

		if (!sym)
			return -LIBBPF_ERRNO__FORMAT;
		if (!sym_is_extern(sym))
			continue;
		ext_name = elf_sym_str(obj, sym->st_name);
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
		ext->is_weak = ELF64_ST_BIND(sym->st_info) == STB_WEAK;

		ext_essent_len = bpf_core_essential_name_len(ext->name);
		ext->essent_name = NULL;
		if (ext_essent_len != strlen(ext->name)) {
			ext->essent_name = strndup(ext->name, ext_essent_len);
			if (!ext->essent_name)
				return -ENOMEM;
		}

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
				pr_warn("extern (kcfg) '%s': type is unsupported\n", ext_name);
				return -ENOTSUP;
			}
		} else if (strcmp(sec_name, KSYMS_SEC) == 0) {
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

static bool prog_is_subprog(const struct bpf_object *obj, const struct bpf_program *prog)
{
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
	switch (obj->efile.secs[shndx].sec_type) {
	case SEC_BSS:
	case SEC_DATA:
	case SEC_RODATA:
		return true;
	default:
		return false;
	}
}

static bool bpf_object__shndx_is_maps(const struct bpf_object *obj,
				      int shndx)
{
	return shndx == obj->efile.btf_maps_shndx;
}

static enum libbpf_map_type
bpf_object__section_to_libbpf_map_type(const struct bpf_object *obj, int shndx)
{
	if (shndx == obj->efile.symbols_shndx)
		return LIBBPF_MAP_KCONFIG;

	switch (obj->efile.secs[shndx].sec_type) {
	case SEC_BSS:
		return LIBBPF_MAP_BSS;
	case SEC_DATA:
		return LIBBPF_MAP_DATA;
	case SEC_RODATA:
		return LIBBPF_MAP_RODATA;
	default:
		return LIBBPF_MAP_UNSPEC;
	}
}

static int bpf_program__record_reloc(struct bpf_program *prog,
				     struct reloc_desc *reloc_desc,
				     __u32 insn_idx, const char *sym_name,
				     const Elf64_Sym *sym, const Elf64_Rel *rel)
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
		int sym_idx = ELF64_R_SYM(rel->r_info);
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
			reloc_desc->type = RELO_EXTERN_CALL;
		else
			reloc_desc->type = RELO_EXTERN_LD64;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->ext_idx = i;
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

	/* arena data relocation */
	if (shdr_idx == obj->efile.arena_data_shndx) {
		reloc_desc->type = RELO_DATA;
		reloc_desc->insn_idx = insn_idx;
		reloc_desc->map_idx = obj->arena_map - obj->maps;
		reloc_desc->sym_off = sym->st_value;
		return 0;
	}

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
		if (map->libbpf_type != type || map->sec_idx != sym->st_shndx)
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

	if (!obj->nr_programs)
		return NULL;

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
bpf_object__collect_prog_relos(struct bpf_object *obj, Elf64_Shdr *shdr, Elf_Data *data)
{
	const char *relo_sec_name, *sec_name;
	size_t sec_idx = shdr->sh_info, sym_idx;
	struct bpf_program *prog;
	struct reloc_desc *relos;
	int err, i, nrels;
	const char *sym_name;
	__u32 insn_idx;
	Elf_Scn *scn;
	Elf_Data *scn_data;
	Elf64_Sym *sym;
	Elf64_Rel *rel;

	if (sec_idx >= obj->efile.sec_cnt)
		return -EINVAL;

	scn = elf_sec_by_idx(obj, sec_idx);
	scn_data = elf_sec_data(obj, scn);
	if (!scn_data)
		return -LIBBPF_ERRNO__FORMAT;

	relo_sec_name = elf_sec_str(obj, shdr->sh_name);
	sec_name = elf_sec_name(obj, scn);
	if (!relo_sec_name || !sec_name)
		return -EINVAL;

	pr_debug("sec '%s': collecting relocation for section(%zu) '%s'\n",
		 relo_sec_name, sec_idx, sec_name);
	nrels = shdr->sh_size / shdr->sh_entsize;

	for (i = 0; i < nrels; i++) {
		rel = elf_rel_by_idx(data, i);
		if (!rel) {
			pr_warn("sec '%s': failed to get relo #%d\n", relo_sec_name, i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		sym_idx = ELF64_R_SYM(rel->r_info);
		sym = elf_sym_by_idx(obj, sym_idx);
		if (!sym) {
			pr_warn("sec '%s': symbol #%zu not found for relo #%d\n",
				relo_sec_name, sym_idx, i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (sym->st_shndx >= obj->efile.sec_cnt) {
			pr_warn("sec '%s': corrupted symbol #%zu pointing to invalid section #%zu for relo #%d\n",
				relo_sec_name, sym_idx, (size_t)sym->st_shndx, i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		if (rel->r_offset % BPF_INSN_SZ || rel->r_offset >= scn_data->d_size) {
			pr_warn("sec '%s': invalid offset 0x%zx for relo #%d\n",
				relo_sec_name, (size_t)rel->r_offset, i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		insn_idx = rel->r_offset / BPF_INSN_SZ;
		/* relocations against static functions are recorded as
		 * relocations against the section that contains a function;
		 * in such case, symbol will be STT_SECTION and sym.st_name
		 * will point to empty string (0), so fetch section name
		 * instead
		 */
		if (ELF64_ST_TYPE(sym->st_info) == STT_SECTION && sym->st_name == 0)
			sym_name = elf_sec_name(obj, elf_sec_by_idx(obj, sym->st_shndx));
		else
			sym_name = elf_sym_str(obj, sym->st_name);
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
						insn_idx, sym_name, sym, rel);
		if (err)
			return err;

		prog->nr_reloc++;
	}
	return 0;
}

static int map_fill_btf_type_info(struct bpf_object *obj, struct bpf_map *map)
{
	int id;

	if (!obj->btf)
		return -ENOENT;

	/* if it's BTF-defined map, we don't need to search for type IDs.
	 * For struct_ops map, it does not need btf_key_type_id and
	 * btf_value_type_id.
	 */
	if (map->sec_idx == obj->efile.btf_maps_shndx || bpf_map__is_struct_ops(map))
		return 0;

	/*
	 * LLVM annotates global data differently in BTF, that is,
	 * only as '.data', '.bss' or '.rodata'.
	 */
	if (!bpf_map__is_internal(map))
		return -ENOENT;

	id = btf__find_by_name(obj->btf, map->real_name);
	if (id < 0)
		return id;

	map->btf_key_type_id = 0;
	map->btf_value_type_id = id;
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

	fp = fopen(file, "re");
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

bool bpf_map__autocreate(const struct bpf_map *map)
{
	return map->autocreate;
}

int bpf_map__set_autocreate(struct bpf_map *map, bool autocreate)
{
	if (map->obj->loaded)
		return libbpf_err(-EBUSY);

	map->autocreate = autocreate;
	return 0;
}

int bpf_map__reuse_fd(struct bpf_map *map, int fd)
{
	struct bpf_map_info info;
	__u32 len = sizeof(info), name_len;
	int new_fd, err;
	char *new_name;

	memset(&info, 0, len);
	err = bpf_map_get_info_by_fd(fd, &info, &len);
	if (err && errno == EINVAL)
		err = bpf_get_map_info_from_fdinfo(fd, &info);
	if (err)
		return libbpf_err(err);

	name_len = strlen(info.name);
	if (name_len == BPF_OBJ_NAME_LEN - 1 && strncmp(map->name, info.name, name_len) == 0)
		new_name = strdup(map->name);
	else
		new_name = strdup(info.name);

	if (!new_name)
		return libbpf_err(-errno);

	/*
	 * Like dup(), but make sure new FD is >= 3 and has O_CLOEXEC set.
	 * This is similar to what we do in ensure_good_fd(), but without
	 * closing original FD.
	 */
	new_fd = fcntl(fd, F_DUPFD_CLOEXEC, 3);
	if (new_fd < 0) {
		err = -errno;
		goto err_free_new_name;
	}

	err = reuse_fd(map->fd, new_fd);
	if (err)
		goto err_free_new_name;

	free(map->name);

	map->name = new_name;
	map->def.type = info.type;
	map->def.key_size = info.key_size;
	map->def.value_size = info.value_size;
	map->def.max_entries = info.max_entries;
	map->def.map_flags = info.map_flags;
	map->btf_key_type_id = info.btf_key_type_id;
	map->btf_value_type_id = info.btf_value_type_id;
	map->reused = true;
	map->map_extra = info.map_extra;

	return 0;

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
	if (map->obj->loaded)
		return libbpf_err(-EBUSY);

	map->def.max_entries = max_entries;

	/* auto-adjust BPF ringbuf map max_entries to be a multiple of page size */
	if (map_is_ringbuf(map))
		map->def.max_entries = adjust_ringbuf_sz(map->def.max_entries);

	return 0;
}

static int bpf_object_prepare_token(struct bpf_object *obj)
{
	const char *bpffs_path;
	int bpffs_fd = -1, token_fd, err;
	bool mandatory;
	enum libbpf_print_level level;

	/* token is explicitly prevented */
	if (obj->token_path && obj->token_path[0] == '\0') {
		pr_debug("object '%s': token is prevented, skipping...\n", obj->name);
		return 0;
	}

	mandatory = obj->token_path != NULL;
	level = mandatory ? LIBBPF_WARN : LIBBPF_DEBUG;

	bpffs_path = obj->token_path ?: BPF_FS_DEFAULT_PATH;
	bpffs_fd = open(bpffs_path, O_DIRECTORY, O_RDWR);
	if (bpffs_fd < 0) {
		err = -errno;
		__pr(level, "object '%s': failed (%d) to open BPF FS mount at '%s'%s\n",
		     obj->name, err, bpffs_path,
		     mandatory ? "" : ", skipping optional step...");
		return mandatory ? err : 0;
	}

	token_fd = bpf_token_create(bpffs_fd, 0);
	close(bpffs_fd);
	if (token_fd < 0) {
		if (!mandatory && token_fd == -ENOENT) {
			pr_debug("object '%s': BPF FS at '%s' doesn't have BPF token delegation set up, skipping...\n",
				 obj->name, bpffs_path);
			return 0;
		}
		__pr(level, "object '%s': failed (%d) to create BPF token from '%s'%s\n",
		     obj->name, token_fd, bpffs_path,
		     mandatory ? "" : ", skipping optional step...");
		return mandatory ? token_fd : 0;
	}

	obj->feat_cache = calloc(1, sizeof(*obj->feat_cache));
	if (!obj->feat_cache) {
		close(token_fd);
		return -ENOMEM;
	}

	obj->token_fd = token_fd;
	obj->feat_cache->token_fd = token_fd;

	return 0;
}

static int
bpf_object__probe_loading(struct bpf_object *obj)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	struct bpf_insn insns[] = {
		BPF_MOV64_IMM(BPF_REG_0, 0),
		BPF_EXIT_INSN(),
	};
	int ret, insn_cnt = ARRAY_SIZE(insns);
	LIBBPF_OPTS(bpf_prog_load_opts, opts,
		.token_fd = obj->token_fd,
		.prog_flags = obj->token_fd ? BPF_F_TOKEN_FD : 0,
	);

	if (obj->gen_loader)
		return 0;

	ret = bump_rlimit_memlock();
	if (ret)
		pr_warn("Failed to bump RLIMIT_MEMLOCK (err = %d), you might need to do it explicitly!\n", ret);

	/* make sure basic loading works */
	ret = bpf_prog_load(BPF_PROG_TYPE_SOCKET_FILTER, NULL, "GPL", insns, insn_cnt, &opts);
	if (ret < 0)
		ret = bpf_prog_load(BPF_PROG_TYPE_TRACEPOINT, NULL, "GPL", insns, insn_cnt, &opts);
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

bool kernel_supports(const struct bpf_object *obj, enum kern_feature_id feat_id)
{
	if (obj->gen_loader)
		/* To generate loader program assume the latest kernel
		 * to avoid doing extra prog_load, map_create syscalls.
		 */
		return true;

	if (obj->token_fd)
		return feat_supported(obj->feat_cache, feat_id);

	return feat_supported(NULL, feat_id);
}

static bool map_is_reuse_compat(const struct bpf_map *map, int map_fd)
{
	struct bpf_map_info map_info;
	char msg[STRERR_BUFSIZE];
	__u32 map_info_len = sizeof(map_info);
	int err;

	memset(&map_info, 0, map_info_len);
	err = bpf_map_get_info_by_fd(map_fd, &map_info, &map_info_len);
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
		map_info.map_flags == map->def.map_flags &&
		map_info.map_extra == map->map_extra);
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
	close(pin_fd);
	if (err)
		return err;

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

static bool map_is_created(const struct bpf_map *map)
{
	return map->obj->loaded || map->reused;
}

static int bpf_object__create_map(struct bpf_object *obj, struct bpf_map *map, bool is_inner)
{
	LIBBPF_OPTS(bpf_map_create_opts, create_attr);
	struct bpf_map_def *def = &map->def;
	const char *map_name = NULL;
	int err = 0, map_fd;

	if (kernel_supports(obj, FEAT_PROG_NAME))
		map_name = map->name;
	create_attr.map_ifindex = map->map_ifindex;
	create_attr.map_flags = def->map_flags;
	create_attr.numa_node = map->numa_node;
	create_attr.map_extra = map->map_extra;
	create_attr.token_fd = obj->token_fd;
	if (obj->token_fd)
		create_attr.map_flags |= BPF_F_TOKEN_FD;

	if (bpf_map__is_struct_ops(map)) {
		create_attr.btf_vmlinux_value_type_id = map->btf_vmlinux_value_type_id;
		if (map->mod_btf_fd >= 0) {
			create_attr.value_type_btf_obj_fd = map->mod_btf_fd;
			create_attr.map_flags |= BPF_F_VTYPE_BTF_OBJ_FD;
		}
	}

	if (obj->btf && btf__fd(obj->btf) >= 0) {
		create_attr.btf_fd = btf__fd(obj->btf);
		create_attr.btf_key_type_id = map->btf_key_type_id;
		create_attr.btf_value_type_id = map->btf_value_type_id;
	}

	if (bpf_map_type__is_map_in_map(def->type)) {
		if (map->inner_map) {
			err = map_set_def_max_entries(map->inner_map);
			if (err)
				return err;
			err = bpf_object__create_map(obj, map->inner_map, true);
			if (err) {
				pr_warn("map '%s': failed to create inner map: %d\n",
					map->name, err);
				return err;
			}
			map->inner_map_fd = map->inner_map->fd;
		}
		if (map->inner_map_fd >= 0)
			create_attr.inner_map_fd = map->inner_map_fd;
	}

	switch (def->type) {
	case BPF_MAP_TYPE_PERF_EVENT_ARRAY:
	case BPF_MAP_TYPE_CGROUP_ARRAY:
	case BPF_MAP_TYPE_STACK_TRACE:
	case BPF_MAP_TYPE_ARRAY_OF_MAPS:
	case BPF_MAP_TYPE_HASH_OF_MAPS:
	case BPF_MAP_TYPE_DEVMAP:
	case BPF_MAP_TYPE_DEVMAP_HASH:
	case BPF_MAP_TYPE_CPUMAP:
	case BPF_MAP_TYPE_XSKMAP:
	case BPF_MAP_TYPE_SOCKMAP:
	case BPF_MAP_TYPE_SOCKHASH:
	case BPF_MAP_TYPE_QUEUE:
	case BPF_MAP_TYPE_STACK:
	case BPF_MAP_TYPE_ARENA:
		create_attr.btf_fd = 0;
		create_attr.btf_key_type_id = 0;
		create_attr.btf_value_type_id = 0;
		map->btf_key_type_id = 0;
		map->btf_value_type_id = 0;
		break;
	case BPF_MAP_TYPE_STRUCT_OPS:
		create_attr.btf_value_type_id = 0;
		break;
	default:
		break;
	}

	if (obj->gen_loader) {
		bpf_gen__map_create(obj->gen_loader, def->type, map_name,
				    def->key_size, def->value_size, def->max_entries,
				    &create_attr, is_inner ? -1 : map - obj->maps);
		/* We keep pretenting we have valid FD to pass various fd >= 0
		 * checks by just keeping original placeholder FDs in place.
		 * See bpf_object__add_map() comment.
		 * This placeholder fd will not be used with any syscall and
		 * will be reset to -1 eventually.
		 */
		map_fd = map->fd;
	} else {
		map_fd = bpf_map_create(def->type, map_name,
					def->key_size, def->value_size,
					def->max_entries, &create_attr);
	}
	if (map_fd < 0 && (create_attr.btf_key_type_id || create_attr.btf_value_type_id)) {
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
		map_fd = bpf_map_create(def->type, map_name,
					def->key_size, def->value_size,
					def->max_entries, &create_attr);
	}

	if (bpf_map_type__is_map_in_map(def->type) && map->inner_map) {
		if (obj->gen_loader)
			map->inner_map->fd = -1;
		bpf_map__destroy(map->inner_map);
		zfree(&map->inner_map);
	}

	if (map_fd < 0)
		return map_fd;

	/* obj->gen_loader case, prevent reuse_fd() from closing map_fd */
	if (map->fd == map_fd)
		return 0;

	/* Keep placeholder FD value but now point it to the BPF map object.
	 * This way everything that relied on this map's FD (e.g., relocated
	 * ldimm64 instructions) will stay valid and won't need adjustments.
	 * map->fd stays valid but now point to what map_fd points to.
	 */
	return reuse_fd(map->fd, map_fd);
}

static int init_map_in_map_slots(struct bpf_object *obj, struct bpf_map *map)
{
	const struct bpf_map *targ_map;
	unsigned int i;
	int fd, err = 0;

	for (i = 0; i < map->init_slots_sz; i++) {
		if (!map->init_slots[i])
			continue;

		targ_map = map->init_slots[i];
		fd = targ_map->fd;

		if (obj->gen_loader) {
			bpf_gen__populate_outer_map(obj->gen_loader,
						    map - obj->maps, i,
						    targ_map - obj->maps);
		} else {
			err = bpf_map_update_elem(map->fd, &i, &fd, 0);
		}
		if (err) {
			err = -errno;
			pr_warn("map '%s': failed to initialize slot [%d] to map '%s' fd=%d: %d\n",
				map->name, i, targ_map->name, fd, err);
			return err;
		}
		pr_debug("map '%s': slot [%d] set to map '%s' fd=%d\n",
			 map->name, i, targ_map->name, fd);
	}

	zfree(&map->init_slots);
	map->init_slots_sz = 0;

	return 0;
}

static int init_prog_array_slots(struct bpf_object *obj, struct bpf_map *map)
{
	const struct bpf_program *targ_prog;
	unsigned int i;
	int fd, err;

	if (obj->gen_loader)
		return -ENOTSUP;

	for (i = 0; i < map->init_slots_sz; i++) {
		if (!map->init_slots[i])
			continue;

		targ_prog = map->init_slots[i];
		fd = bpf_program__fd(targ_prog);

		err = bpf_map_update_elem(map->fd, &i, &fd, 0);
		if (err) {
			err = -errno;
			pr_warn("map '%s': failed to initialize slot [%d] to prog '%s' fd=%d: %d\n",
				map->name, i, targ_prog->name, fd, err);
			return err;
		}
		pr_debug("map '%s': slot [%d] set to prog '%s' fd=%d\n",
			 map->name, i, targ_prog->name, fd);
	}

	zfree(&map->init_slots);
	map->init_slots_sz = 0;

	return 0;
}

static int bpf_object_init_prog_arrays(struct bpf_object *obj)
{
	struct bpf_map *map;
	int i, err;

	for (i = 0; i < obj->nr_maps; i++) {
		map = &obj->maps[i];

		if (!map->init_slots_sz || map->def.type != BPF_MAP_TYPE_PROG_ARRAY)
			continue;

		err = init_prog_array_slots(obj, map);
		if (err < 0)
			return err;
	}
	return 0;
}

static int map_set_def_max_entries(struct bpf_map *map)
{
	if (map->def.type == BPF_MAP_TYPE_PERF_EVENT_ARRAY && !map->def.max_entries) {
		int nr_cpus;

		nr_cpus = libbpf_num_possible_cpus();
		if (nr_cpus < 0) {
			pr_warn("map '%s': failed to determine number of system CPUs: %d\n",
				map->name, nr_cpus);
			return nr_cpus;
		}
		pr_debug("map '%s': setting size to %d\n", map->name, nr_cpus);
		map->def.max_entries = nr_cpus;
	}

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

		/* To support old kernels, we skip creating global data maps
		 * (.rodata, .data, .kconfig, etc); later on, during program
		 * loading, if we detect that at least one of the to-be-loaded
		 * programs is referencing any global data map, we'll error
		 * out with program name and relocation index logged.
		 * This approach allows to accommodate Clang emitting
		 * unnecessary .rodata.str1.1 sections for string literals,
		 * but also it allows to have CO-RE applications that use
		 * global variables in some of BPF programs, but not others.
		 * If those global variable-using programs are not loaded at
		 * runtime due to bpf_program__set_autoload(prog, false),
		 * bpf_object loading will succeed just fine even on old
		 * kernels.
		 */
		if (bpf_map__is_internal(map) && !kernel_supports(obj, FEAT_GLOBAL_DATA))
			map->autocreate = false;

		if (!map->autocreate) {
			pr_debug("map '%s': skipped auto-creating...\n", map->name);
			continue;
		}

		err = map_set_def_max_entries(map);
		if (err)
			goto err_out;

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

		if (map->reused) {
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
				if (err < 0)
					goto err_out;
			}
			if (map->def.type == BPF_MAP_TYPE_ARENA) {
				map->mmaped = mmap((void *)(long)map->map_extra,
						   bpf_map_mmap_sz(map), PROT_READ | PROT_WRITE,
						   map->map_extra ? MAP_SHARED | MAP_FIXED : MAP_SHARED,
						   map->fd, 0);
				if (map->mmaped == MAP_FAILED) {
					err = -errno;
					map->mmaped = NULL;
					pr_warn("map '%s': failed to mmap arena: %d\n",
						map->name, err);
					return err;
				}
				if (obj->arena_data) {
					memcpy(map->mmaped, obj->arena_data, obj->arena_data_sz);
					zfree(&obj->arena_data);
				}
			}
			if (map->init_slots_sz && map->def.type != BPF_MAP_TYPE_PROG_ARRAY) {
				err = init_map_in_map_slots(obj, map);
				if (err < 0)
					goto err_out;
			}
		}

		if (map->pin_path && !map->pinned) {
			err = bpf_map__pin(map, NULL);
			if (err) {
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

void bpf_core_free_cands(struct bpf_core_cand_list *cands)
{
	if (!cands)
		return;

	free(cands->cands);
	free(cands);
}

int bpf_core_add_cands(struct bpf_core_cand *local_cand,
		       size_t local_essent_len,
		       const struct btf *targ_btf,
		       const char *targ_btf_name,
		       int targ_start_id,
		       struct bpf_core_cand_list *cands)
{
	struct bpf_core_cand *new_cands, *cand;
	const struct btf_type *t, *local_t;
	const char *targ_name, *local_name;
	size_t targ_essent_len;
	int n, i;

	local_t = btf__type_by_id(local_cand->btf, local_cand->id);
	local_name = btf__str_by_offset(local_cand->btf, local_t->name_off);

	n = btf__type_cnt(targ_btf);
	for (i = targ_start_id; i < n; i++) {
		t = btf__type_by_id(targ_btf, i);
		if (!btf_kind_core_compat(t, local_t))
			continue;

		targ_name = btf__name_by_offset(targ_btf, t->name_off);
		if (str_is_empty(targ_name))
			continue;

		targ_essent_len = bpf_core_essential_name_len(targ_name);
		if (targ_essent_len != local_essent_len)
			continue;

		if (strncmp(local_name, targ_name, local_essent_len) != 0)
			continue;

		pr_debug("CO-RE relocating [%d] %s %s: found target candidate [%d] %s %s in [%s]\n",
			 local_cand->id, btf_kind_str(local_t),
			 local_name, i, btf_kind_str(t), targ_name,
			 targ_btf_name);
		new_cands = libbpf_reallocarray(cands->cands, cands->len + 1,
					      sizeof(*cands->cands));
		if (!new_cands)
			return -ENOMEM;

		cand = &new_cands[cands->len];
		cand->btf = targ_btf;
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
		if (err && errno == EPERM) {
			pr_debug("skipping module BTFs loading, missing privileges\n");
			return 0;
		}
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

		err = bpf_btf_get_info_by_fd(fd, &info, &len);
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
	const struct btf_type *local_t;
	const char *local_name;
	size_t local_essent_len;
	int err, i;

	local_cand.btf = local_btf;
	local_cand.id = local_type_id;
	local_t = btf__type_by_id(local_btf, local_type_id);
	if (!local_t)
		return ERR_PTR(-EINVAL);

	local_name = btf__name_by_offset(local_btf, local_t->name_off);
	if (str_is_empty(local_name))
		return ERR_PTR(-EINVAL);
	local_essent_len = bpf_core_essential_name_len(local_name);

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
					 btf__type_cnt(obj->btf_vmlinux),
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
	return __bpf_core_types_are_compat(local_btf, local_id, targ_btf, targ_id, 32);
}

int bpf_core_types_match(const struct btf *local_btf, __u32 local_id,
			 const struct btf *targ_btf, __u32 targ_id)
{
	return __bpf_core_types_match(local_btf, local_id, targ_btf, targ_id, false, 32);
}

static size_t bpf_core_hash_fn(const long key, void *ctx)
{
	return key;
}

static bool bpf_core_equal_fn(const long k1, const long k2, void *ctx)
{
	return k1 == k2;
}

static int record_relo_core(struct bpf_program *prog,
			    const struct bpf_core_relo *core_relo, int insn_idx)
{
	struct reloc_desc *relos, *relo;

	relos = libbpf_reallocarray(prog->reloc_desc,
				    prog->nr_reloc + 1, sizeof(*relos));
	if (!relos)
		return -ENOMEM;
	relo = &relos[prog->nr_reloc];
	relo->type = RELO_CORE;
	relo->insn_idx = insn_idx;
	relo->core_relo = core_relo;
	prog->reloc_desc = relos;
	prog->nr_reloc++;
	return 0;
}

static const struct bpf_core_relo *find_relo_core(struct bpf_program *prog, int insn_idx)
{
	struct reloc_desc *relo;
	int i;

	for (i = 0; i < prog->nr_reloc; i++) {
		relo = &prog->reloc_desc[i];
		if (relo->type != RELO_CORE || relo->insn_idx != insn_idx)
			continue;

		return relo->core_relo;
	}

	return NULL;
}

static int bpf_core_resolve_relo(struct bpf_program *prog,
				 const struct bpf_core_relo *relo,
				 int relo_idx,
				 const struct btf *local_btf,
				 struct hashmap *cand_cache,
				 struct bpf_core_relo_res *targ_res)
{
	struct bpf_core_spec specs_scratch[3] = {};
	struct bpf_core_cand_list *cands = NULL;
	const char *prog_name = prog->name;
	const struct btf_type *local_type;
	const char *local_name;
	__u32 local_id = relo->type_id;
	int err;

	local_type = btf__type_by_id(local_btf, local_id);
	if (!local_type)
		return -EINVAL;

	local_name = btf__name_by_offset(local_btf, local_type->name_off);
	if (!local_name)
		return -EINVAL;

	if (relo->kind != BPF_CORE_TYPE_ID_LOCAL &&
	    !hashmap__find(cand_cache, local_id, &cands)) {
		cands = bpf_core_find_cands(prog->obj, local_btf, local_id);
		if (IS_ERR(cands)) {
			pr_warn("prog '%s': relo #%d: target candidate search failed for [%d] %s %s: %ld\n",
				prog_name, relo_idx, local_id, btf_kind_str(local_type),
				local_name, PTR_ERR(cands));
			return PTR_ERR(cands);
		}
		err = hashmap__set(cand_cache, local_id, cands, NULL, NULL);
		if (err) {
			bpf_core_free_cands(cands);
			return err;
		}
	}

	return bpf_core_calc_relo_insn(prog_name, relo, relo_idx, local_btf, cands, specs_scratch,
				       targ_res);
}

static int
bpf_object__relocate_core(struct bpf_object *obj, const char *targ_btf_path)
{
	const struct btf_ext_info_sec *sec;
	struct bpf_core_relo_res targ_res;
	const struct bpf_core_relo *rec;
	const struct btf_ext_info *seg;
	struct hashmap_entry *entry;
	struct hashmap *cand_cache = NULL;
	struct bpf_program *prog;
	struct bpf_insn *insn;
	const char *sec_name;
	int i, err = 0, insn_idx, sec_idx, sec_num;

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
	sec_num = 0;
	for_each_btf_ext_sec(seg, sec) {
		sec_idx = seg->sec_idxs[sec_num];
		sec_num++;

		sec_name = btf__name_by_offset(obj->btf, sec->sec_name_off);
		if (str_is_empty(sec_name)) {
			err = -EINVAL;
			goto out;
		}

		pr_debug("sec '%s': found %d CO-RE relocations\n", sec_name, sec->num_info);

		for_each_btf_ext_rec(seg, sec, i, rec) {
			if (rec->insn_off % BPF_INSN_SZ)
				return -EINVAL;
			insn_idx = rec->insn_off / BPF_INSN_SZ;
			prog = find_prog_by_sec_insn(obj, sec_idx, insn_idx);
			if (!prog) {
				/* When __weak subprog is "overridden" by another instance
				 * of the subprog from a different object file, linker still
				 * appends all the .BTF.ext info that used to belong to that
				 * eliminated subprogram.
				 * This is similar to what x86-64 linker does for relocations.
				 * So just ignore such relocations just like we ignore
				 * subprog instructions when discovering subprograms.
				 */
				pr_debug("sec '%s': skipping CO-RE relocation #%d for insn #%d belonging to eliminated weak subprogram\n",
					 sec_name, i, insn_idx);
				continue;
			}
			/* no need to apply CO-RE relocation if the program is
			 * not going to be loaded
			 */
			if (!prog->autoload)
				continue;

			/* adjust insn_idx from section frame of reference to the local
			 * program's frame of reference; (sub-)program code is not yet
			 * relocated, so it's enough to just subtract in-section offset
			 */
			insn_idx = insn_idx - prog->sec_insn_off;
			if (insn_idx >= prog->insns_cnt)
				return -EINVAL;
			insn = &prog->insns[insn_idx];

			err = record_relo_core(prog, rec, insn_idx);
			if (err) {
				pr_warn("prog '%s': relo #%d: failed to record relocation: %d\n",
					prog->name, i, err);
				goto out;
			}

			if (prog->obj->gen_loader)
				continue;

			err = bpf_core_resolve_relo(prog, rec, i, obj->btf, cand_cache, &targ_res);
			if (err) {
				pr_warn("prog '%s': relo #%d: failed to relocate: %d\n",
					prog->name, i, err);
				goto out;
			}

			err = bpf_core_patch_insn(prog->name, insn, insn_idx, rec, i, &targ_res);
			if (err) {
				pr_warn("prog '%s': relo #%d: failed to patch insn #%u: %d\n",
					prog->name, i, insn_idx, err);
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
			bpf_core_free_cands(entry->pvalue);
		}
		hashmap__free(cand_cache);
	}
	return err;
}

/* base map load ldimm64 special constant, used also for log fixup logic */
#define POISON_LDIMM64_MAP_BASE 2001000000
#define POISON_LDIMM64_MAP_PFX "200100"

static void poison_map_ldimm64(struct bpf_program *prog, int relo_idx,
			       int insn_idx, struct bpf_insn *insn,
			       int map_idx, const struct bpf_map *map)
{
	int i;

	pr_debug("prog '%s': relo #%d: poisoning insn #%d that loads map #%d '%s'\n",
		 prog->name, relo_idx, insn_idx, map_idx, map->name);

	/* we turn single ldimm64 into two identical invalid calls */
	for (i = 0; i < 2; i++) {
		insn->code = BPF_JMP | BPF_CALL;
		insn->dst_reg = 0;
		insn->src_reg = 0;
		insn->off = 0;
		/* if this instruction is reachable (not a dead code),
		 * verifier will complain with something like:
		 * invalid func unknown#2001000123
		 * where lower 123 is map index into obj->maps[] array
		 */
		insn->imm = POISON_LDIMM64_MAP_BASE + map_idx;

		insn++;
	}
}

/* unresolved kfunc call special constant, used also for log fixup logic */
#define POISON_CALL_KFUNC_BASE 2002000000
#define POISON_CALL_KFUNC_PFX "2002"

static void poison_kfunc_call(struct bpf_program *prog, int relo_idx,
			      int insn_idx, struct bpf_insn *insn,
			      int ext_idx, const struct extern_desc *ext)
{
	pr_debug("prog '%s': relo #%d: poisoning insn #%d that calls kfunc '%s'\n",
		 prog->name, relo_idx, insn_idx, ext->name);

	/* we turn kfunc call into invalid helper call with identifiable constant */
	insn->code = BPF_JMP | BPF_CALL;
	insn->dst_reg = 0;
	insn->src_reg = 0;
	insn->off = 0;
	/* if this instruction is reachable (not a dead code),
	 * verifier will complain with something like:
	 * invalid func unknown#2001000123
	 * where lower 123 is extern index into obj->externs[] array
	 */
	insn->imm = POISON_CALL_KFUNC_BASE + ext_idx;
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
		const struct bpf_map *map;
		struct extern_desc *ext;

		switch (relo->type) {
		case RELO_LD64:
			map = &obj->maps[relo->map_idx];
			if (obj->gen_loader) {
				insn[0].src_reg = BPF_PSEUDO_MAP_IDX;
				insn[0].imm = relo->map_idx;
			} else if (map->autocreate) {
				insn[0].src_reg = BPF_PSEUDO_MAP_FD;
				insn[0].imm = map->fd;
			} else {
				poison_map_ldimm64(prog, i, relo->insn_idx, insn,
						   relo->map_idx, map);
			}
			break;
		case RELO_DATA:
			map = &obj->maps[relo->map_idx];
			insn[1].imm = insn[0].imm + relo->sym_off;
			if (obj->gen_loader) {
				insn[0].src_reg = BPF_PSEUDO_MAP_IDX_VALUE;
				insn[0].imm = relo->map_idx;
			} else if (map->autocreate) {
				insn[0].src_reg = BPF_PSEUDO_MAP_VALUE;
				insn[0].imm = map->fd;
			} else {
				poison_map_ldimm64(prog, i, relo->insn_idx, insn,
						   relo->map_idx, map);
			}
			break;
		case RELO_EXTERN_LD64:
			ext = &obj->externs[relo->ext_idx];
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
		case RELO_EXTERN_CALL:
			ext = &obj->externs[relo->ext_idx];
			insn[0].src_reg = BPF_PSEUDO_KFUNC_CALL;
			if (ext->is_set) {
				insn[0].imm = ext->ksym.kernel_btf_id;
				insn[0].off = ext->ksym.btf_fd_idx;
			} else { /* unresolved weak kfunc call */
				poison_kfunc_call(prog, i, relo->insn_idx, insn,
						  relo->ext_idx, ext);
			}
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
		case RELO_CORE:
			/* will be handled by bpf_program_record_relos() */
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
	int i, sec_num, sec_idx, off_adj;

	sec_num = 0;
	for_each_btf_ext_sec(ext_info, sec) {
		sec_idx = ext_info->sec_idxs[sec_num];
		sec_num++;
		if (prog->sec_idx != sec_idx)
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
	 * support func/line info
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
	if (!prog->nr_reloc)
		return NULL;
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
	/* if new count is zero, reallocarray can return a valid NULL result;
	 * in this case the previous pointer will be freed, so we *have to*
	 * reassign old pointer to the new value (even if it's NULL)
	 */
	if (!relos && new_cnt)
		return -ENOMEM;
	if (subprog->nr_reloc)
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
bpf_object__append_subprog_code(struct bpf_object *obj, struct bpf_program *main_prog,
				struct bpf_program *subprog)
{
       struct bpf_insn *insns;
       size_t new_cnt;
       int err;

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
       return 0;
}

static int
bpf_object__reloc_code(struct bpf_object *obj, struct bpf_program *main_prog,
		       struct bpf_program *prog)
{
	size_t sub_insn_idx, insn_idx;
	struct bpf_program *subprog;
	struct reloc_desc *relo;
	struct bpf_insn *insn;
	int err;

	err = reloc_prog_func_and_line_info(obj, main_prog, prog);
	if (err)
		return err;

	for (insn_idx = 0; insn_idx < prog->sec_insn_cnt; insn_idx++) {
		insn = &main_prog->insns[prog->sub_insn_off + insn_idx];
		if (!insn_is_subprog_call(insn) && !insn_is_pseudo_func(insn))
			continue;

		relo = find_prog_insn_relo(prog, insn_idx);
		if (relo && relo->type == RELO_EXTERN_CALL)
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
			err = bpf_object__append_subprog_code(obj, main_prog, subprog);
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
		 * different main programs
		 */
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

static void bpf_object__sort_relos(struct bpf_object *obj)
{
	int i;

	for (i = 0; i < obj->nr_programs; i++) {
		struct bpf_program *p = &obj->programs[i];

		if (!p->nr_reloc)
			continue;

		qsort(p->reloc_desc, p->nr_reloc, sizeof(*p->reloc_desc), cmp_relocs);
	}
}

static int bpf_prog_assign_exc_cb(struct bpf_object *obj, struct bpf_program *prog)
{
	const char *str = "exception_callback:";
	size_t pfx_len = strlen(str);
	int i, j, n;

	if (!obj->btf || !kernel_supports(obj, FEAT_BTF_DECL_TAG))
		return 0;

	n = btf__type_cnt(obj->btf);
	for (i = 1; i < n; i++) {
		const char *name;
		struct btf_type *t;

		t = btf_type_by_id(obj->btf, i);
		if (!btf_is_decl_tag(t) || btf_decl_tag(t)->component_idx != -1)
			continue;

		name = btf__str_by_offset(obj->btf, t->name_off);
		if (strncmp(name, str, pfx_len) != 0)
			continue;

		t = btf_type_by_id(obj->btf, t->type);
		if (!btf_is_func(t) || btf_func_linkage(t) != BTF_FUNC_GLOBAL) {
			pr_warn("prog '%s': exception_callback:<value> decl tag not applied to the main program\n",
				prog->name);
			return -EINVAL;
		}
		if (strcmp(prog->name, btf__str_by_offset(obj->btf, t->name_off)) != 0)
			continue;
		/* Multiple callbacks are specified for the same prog,
		 * the verifier will eventually return an error for this
		 * case, hence simply skip appending a subprog.
		 */
		if (prog->exception_cb_idx >= 0) {
			prog->exception_cb_idx = -1;
			break;
		}

		name += pfx_len;
		if (str_is_empty(name)) {
			pr_warn("prog '%s': exception_callback:<value> decl tag contains empty value\n",
				prog->name);
			return -EINVAL;
		}

		for (j = 0; j < obj->nr_programs; j++) {
			struct bpf_program *subprog = &obj->programs[j];

			if (!prog_is_subprog(obj, subprog))
				continue;
			if (strcmp(name, subprog->name) != 0)
				continue;
			/* Enforce non-hidden, as from verifier point of
			 * view it expects global functions, whereas the
			 * mark_btf_static fixes up linkage as static.
			 */
			if (!subprog->sym_global || subprog->mark_btf_static) {
				pr_warn("prog '%s': exception callback %s must be a global non-hidden function\n",
					prog->name, subprog->name);
				return -EINVAL;
			}
			/* Let's see if we already saw a static exception callback with the same name */
			if (prog->exception_cb_idx >= 0) {
				pr_warn("prog '%s': multiple subprogs with same name as exception callback '%s'\n",
					prog->name, subprog->name);
				return -EINVAL;
			}
			prog->exception_cb_idx = j;
			break;
		}

		if (prog->exception_cb_idx >= 0)
			continue;

		pr_warn("prog '%s': cannot find exception callback '%s'\n", prog->name, name);
		return -ENOENT;
	}

	return 0;
}

static struct {
	enum bpf_prog_type prog_type;
	const char *ctx_name;
} global_ctx_map[] = {
	{ BPF_PROG_TYPE_CGROUP_DEVICE,           "bpf_cgroup_dev_ctx" },
	{ BPF_PROG_TYPE_CGROUP_SKB,              "__sk_buff" },
	{ BPF_PROG_TYPE_CGROUP_SOCK,             "bpf_sock" },
	{ BPF_PROG_TYPE_CGROUP_SOCK_ADDR,        "bpf_sock_addr" },
	{ BPF_PROG_TYPE_CGROUP_SOCKOPT,          "bpf_sockopt" },
	{ BPF_PROG_TYPE_CGROUP_SYSCTL,           "bpf_sysctl" },
	{ BPF_PROG_TYPE_FLOW_DISSECTOR,          "__sk_buff" },
	{ BPF_PROG_TYPE_KPROBE,                  "bpf_user_pt_regs_t" },
	{ BPF_PROG_TYPE_LWT_IN,                  "__sk_buff" },
	{ BPF_PROG_TYPE_LWT_OUT,                 "__sk_buff" },
	{ BPF_PROG_TYPE_LWT_SEG6LOCAL,           "__sk_buff" },
	{ BPF_PROG_TYPE_LWT_XMIT,                "__sk_buff" },
	{ BPF_PROG_TYPE_NETFILTER,               "bpf_nf_ctx" },
	{ BPF_PROG_TYPE_PERF_EVENT,              "bpf_perf_event_data" },
	{ BPF_PROG_TYPE_RAW_TRACEPOINT,          "bpf_raw_tracepoint_args" },
	{ BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE, "bpf_raw_tracepoint_args" },
	{ BPF_PROG_TYPE_SCHED_ACT,               "__sk_buff" },
	{ BPF_PROG_TYPE_SCHED_CLS,               "__sk_buff" },
	{ BPF_PROG_TYPE_SK_LOOKUP,               "bpf_sk_lookup" },
	{ BPF_PROG_TYPE_SK_MSG,                  "sk_msg_md" },
	{ BPF_PROG_TYPE_SK_REUSEPORT,            "sk_reuseport_md" },
	{ BPF_PROG_TYPE_SK_SKB,                  "__sk_buff" },
	{ BPF_PROG_TYPE_SOCK_OPS,                "bpf_sock_ops" },
	{ BPF_PROG_TYPE_SOCKET_FILTER,           "__sk_buff" },
	{ BPF_PROG_TYPE_XDP,                     "xdp_md" },
	/* all other program types don't have "named" context structs */
};

/* forward declarations for arch-specific underlying types of bpf_user_pt_regs_t typedef,
 * for below __builtin_types_compatible_p() checks;
 * with this approach we don't need any extra arch-specific #ifdef guards
 */
struct pt_regs;
struct user_pt_regs;
struct user_regs_struct;

static bool need_func_arg_type_fixup(const struct btf *btf, const struct bpf_program *prog,
				     const char *subprog_name, int arg_idx,
				     int arg_type_id, const char *ctx_name)
{
	const struct btf_type *t;
	const char *tname;

	/* check if existing parameter already matches verifier expectations */
	t = skip_mods_and_typedefs(btf, arg_type_id, NULL);
	if (!btf_is_ptr(t))
		goto out_warn;

	/* typedef bpf_user_pt_regs_t is a special PITA case, valid for kprobe
	 * and perf_event programs, so check this case early on and forget
	 * about it for subsequent checks
	 */
	while (btf_is_mod(t))
		t = btf__type_by_id(btf, t->type);
	if (btf_is_typedef(t) &&
	    (prog->type == BPF_PROG_TYPE_KPROBE || prog->type == BPF_PROG_TYPE_PERF_EVENT)) {
		tname = btf__str_by_offset(btf, t->name_off) ?: "<anon>";
		if (strcmp(tname, "bpf_user_pt_regs_t") == 0)
			return false; /* canonical type for kprobe/perf_event */
	}

	/* now we can ignore typedefs moving forward */
	t = skip_mods_and_typedefs(btf, t->type, NULL);

	/* if it's `void *`, definitely fix up BTF info */
	if (btf_is_void(t))
		return true;

	/* if it's already proper canonical type, no need to fix up */
	tname = btf__str_by_offset(btf, t->name_off) ?: "<anon>";
	if (btf_is_struct(t) && strcmp(tname, ctx_name) == 0)
		return false;

	/* special cases */
	switch (prog->type) {
	case BPF_PROG_TYPE_KPROBE:
		/* `struct pt_regs *` is expected, but we need to fix up */
		if (btf_is_struct(t) && strcmp(tname, "pt_regs") == 0)
			return true;
		break;
	case BPF_PROG_TYPE_PERF_EVENT:
		if (__builtin_types_compatible_p(bpf_user_pt_regs_t, struct pt_regs) &&
		    btf_is_struct(t) && strcmp(tname, "pt_regs") == 0)
			return true;
		if (__builtin_types_compatible_p(bpf_user_pt_regs_t, struct user_pt_regs) &&
		    btf_is_struct(t) && strcmp(tname, "user_pt_regs") == 0)
			return true;
		if (__builtin_types_compatible_p(bpf_user_pt_regs_t, struct user_regs_struct) &&
		    btf_is_struct(t) && strcmp(tname, "user_regs_struct") == 0)
			return true;
		break;
	case BPF_PROG_TYPE_RAW_TRACEPOINT:
	case BPF_PROG_TYPE_RAW_TRACEPOINT_WRITABLE:
		/* allow u64* as ctx */
		if (btf_is_int(t) && t->size == 8)
			return true;
		break;
	default:
		break;
	}

out_warn:
	pr_warn("prog '%s': subprog '%s' arg#%d is expected to be of `struct %s *` type\n",
		prog->name, subprog_name, arg_idx, ctx_name);
	return false;
}

static int clone_func_btf_info(struct btf *btf, int orig_fn_id, struct bpf_program *prog)
{
	int fn_id, fn_proto_id, ret_type_id, orig_proto_id;
	int i, err, arg_cnt, fn_name_off, linkage;
	struct btf_type *fn_t, *fn_proto_t, *t;
	struct btf_param *p;

	/* caller already validated FUNC -> FUNC_PROTO validity */
	fn_t = btf_type_by_id(btf, orig_fn_id);
	fn_proto_t = btf_type_by_id(btf, fn_t->type);

	/* Note that each btf__add_xxx() operation invalidates
	 * all btf_type and string pointers, so we need to be
	 * very careful when cloning BTF types. BTF type
	 * pointers have to be always refetched. And to avoid
	 * problems with invalidated string pointers, we
	 * add empty strings initially, then just fix up
	 * name_off offsets in place. Offsets are stable for
	 * existing strings, so that works out.
	 */
	fn_name_off = fn_t->name_off; /* we are about to invalidate fn_t */
	linkage = btf_func_linkage(fn_t);
	orig_proto_id = fn_t->type; /* original FUNC_PROTO ID */
	ret_type_id = fn_proto_t->type; /* fn_proto_t will be invalidated */
	arg_cnt = btf_vlen(fn_proto_t);

	/* clone FUNC_PROTO and its params */
	fn_proto_id = btf__add_func_proto(btf, ret_type_id);
	if (fn_proto_id < 0)
		return -EINVAL;

	for (i = 0; i < arg_cnt; i++) {
		int name_off;

		/* copy original parameter data */
		t = btf_type_by_id(btf, orig_proto_id);
		p = &btf_params(t)[i];
		name_off = p->name_off;

		err = btf__add_func_param(btf, "", p->type);
		if (err)
			return err;

		fn_proto_t = btf_type_by_id(btf, fn_proto_id);
		p = &btf_params(fn_proto_t)[i];
		p->name_off = name_off; /* use remembered str offset */
	}

	/* clone FUNC now, btf__add_func() enforces non-empty name, so use
	 * entry program's name as a placeholder, which we replace immediately
	 * with original name_off
	 */
	fn_id = btf__add_func(btf, prog->name, linkage, fn_proto_id);
	if (fn_id < 0)
		return -EINVAL;

	fn_t = btf_type_by_id(btf, fn_id);
	fn_t->name_off = fn_name_off; /* reuse original string */

	return fn_id;
}

/* Check if main program or global subprog's function prototype has `arg:ctx`
 * argument tags, and, if necessary, substitute correct type to match what BPF
 * verifier would expect, taking into account specific program type. This
 * allows to support __arg_ctx tag transparently on old kernels that don't yet
 * have a native support for it in the verifier, making user's life much
 * easier.
 */
static int bpf_program_fixup_func_info(struct bpf_object *obj, struct bpf_program *prog)
{
	const char *ctx_name = NULL, *ctx_tag = "arg:ctx", *fn_name;
	struct bpf_func_info_min *func_rec;
	struct btf_type *fn_t, *fn_proto_t;
	struct btf *btf = obj->btf;
	const struct btf_type *t;
	struct btf_param *p;
	int ptr_id = 0, struct_id, tag_id, orig_fn_id;
	int i, n, arg_idx, arg_cnt, err, rec_idx;
	int *orig_ids;

	/* no .BTF.ext, no problem */
	if (!obj->btf_ext || !prog->func_info)
		return 0;

	/* don't do any fix ups if kernel natively supports __arg_ctx */
	if (kernel_supports(obj, FEAT_ARG_CTX_TAG))
		return 0;

	/* some BPF program types just don't have named context structs, so
	 * this fallback mechanism doesn't work for them
	 */
	for (i = 0; i < ARRAY_SIZE(global_ctx_map); i++) {
		if (global_ctx_map[i].prog_type != prog->type)
			continue;
		ctx_name = global_ctx_map[i].ctx_name;
		break;
	}
	if (!ctx_name)
		return 0;

	/* remember original func BTF IDs to detect if we already cloned them */
	orig_ids = calloc(prog->func_info_cnt, sizeof(*orig_ids));
	if (!orig_ids)
		return -ENOMEM;
	for (i = 0; i < prog->func_info_cnt; i++) {
		func_rec = prog->func_info + prog->func_info_rec_size * i;
		orig_ids[i] = func_rec->type_id;
	}

	/* go through each DECL_TAG with "arg:ctx" and see if it points to one
	 * of our subprogs; if yes and subprog is global and needs adjustment,
	 * clone and adjust FUNC -> FUNC_PROTO combo
	 */
	for (i = 1, n = btf__type_cnt(btf); i < n; i++) {
		/* only DECL_TAG with "arg:ctx" value are interesting */
		t = btf__type_by_id(btf, i);
		if (!btf_is_decl_tag(t))
			continue;
		if (strcmp(btf__str_by_offset(btf, t->name_off), ctx_tag) != 0)
			continue;

		/* only global funcs need adjustment, if at all */
		orig_fn_id = t->type;
		fn_t = btf_type_by_id(btf, orig_fn_id);
		if (!btf_is_func(fn_t) || btf_func_linkage(fn_t) != BTF_FUNC_GLOBAL)
			continue;

		/* sanity check FUNC -> FUNC_PROTO chain, just in case */
		fn_proto_t = btf_type_by_id(btf, fn_t->type);
		if (!fn_proto_t || !btf_is_func_proto(fn_proto_t))
			continue;

		/* find corresponding func_info record */
		func_rec = NULL;
		for (rec_idx = 0; rec_idx < prog->func_info_cnt; rec_idx++) {
			if (orig_ids[rec_idx] == t->type) {
				func_rec = prog->func_info + prog->func_info_rec_size * rec_idx;
				break;
			}
		}
		/* current main program doesn't call into this subprog */
		if (!func_rec)
			continue;

		/* some more sanity checking of DECL_TAG */
		arg_cnt = btf_vlen(fn_proto_t);
		arg_idx = btf_decl_tag(t)->component_idx;
		if (arg_idx < 0 || arg_idx >= arg_cnt)
			continue;

		/* check if we should fix up argument type */
		p = &btf_params(fn_proto_t)[arg_idx];
		fn_name = btf__str_by_offset(btf, fn_t->name_off) ?: "<anon>";
		if (!need_func_arg_type_fixup(btf, prog, fn_name, arg_idx, p->type, ctx_name))
			continue;

		/* clone fn/fn_proto, unless we already did it for another arg */
		if (func_rec->type_id == orig_fn_id) {
			int fn_id;

			fn_id = clone_func_btf_info(btf, orig_fn_id, prog);
			if (fn_id < 0) {
				err = fn_id;
				goto err_out;
			}

			/* point func_info record to a cloned FUNC type */
			func_rec->type_id = fn_id;
		}

		/* create PTR -> STRUCT type chain to mark PTR_TO_CTX argument;
		 * we do it just once per main BPF program, as all global
		 * funcs share the same program type, so need only PTR ->
		 * STRUCT type chain
		 */
		if (ptr_id == 0) {
			struct_id = btf__add_struct(btf, ctx_name, 0);
			ptr_id = btf__add_ptr(btf, struct_id);
			if (ptr_id < 0 || struct_id < 0) {
				err = -EINVAL;
				goto err_out;
			}
		}

		/* for completeness, clone DECL_TAG and point it to cloned param */
		tag_id = btf__add_decl_tag(btf, ctx_tag, func_rec->type_id, arg_idx);
		if (tag_id < 0) {
			err = -EINVAL;
			goto err_out;
		}

		/* all the BTF manipulations invalidated pointers, refetch them */
		fn_t = btf_type_by_id(btf, func_rec->type_id);
		fn_proto_t = btf_type_by_id(btf, fn_t->type);

		/* fix up type ID pointed to by param */
		p = &btf_params(fn_proto_t)[arg_idx];
		p->type = ptr_id;
	}

	free(orig_ids);
	return 0;
err_out:
	free(orig_ids);
	return err;
}

static int bpf_object__relocate(struct bpf_object *obj, const char *targ_btf_path)
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
		bpf_object__sort_relos(obj);
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
		if (!prog->autoload)
			continue;

		err = bpf_object__relocate_calls(obj, prog);
		if (err) {
			pr_warn("prog '%s': failed to relocate calls: %d\n",
				prog->name, err);
			return err;
		}

		err = bpf_prog_assign_exc_cb(obj, prog);
		if (err)
			return err;
		/* Now, also append exception callback if it has not been done already. */
		if (prog->exception_cb_idx >= 0) {
			struct bpf_program *subprog = &obj->programs[prog->exception_cb_idx];

			/* Calling exception callback directly is disallowed, which the
			 * verifier will reject later. In case it was processed already,
			 * we can skip this step, otherwise for all other valid cases we
			 * have to append exception callback now.
			 */
			if (subprog->sub_insn_off == 0) {
				err = bpf_object__append_subprog_code(obj, prog, subprog);
				if (err)
					return err;
				err = bpf_object__reloc_code(obj, prog, subprog);
				if (err)
					return err;
			}
		}
	}
	for (i = 0; i < obj->nr_programs; i++) {
		prog = &obj->programs[i];
		if (prog_is_subprog(obj, prog))
			continue;
		if (!prog->autoload)
			continue;

		/* Process data relos for main programs */
		err = bpf_object__relocate_data(obj, prog);
		if (err) {
			pr_warn("prog '%s': failed to relocate data references: %d\n",
				prog->name, err);
			return err;
		}

		/* Fix up .BTF.ext information, if necessary */
		err = bpf_program_fixup_func_info(obj, prog);
		if (err) {
			pr_warn("prog '%s': failed to perform .BTF.ext fix ups: %d\n",
				prog->name, err);
			return err;
		}
	}

	return 0;
}

static int bpf_object__collect_st_ops_relos(struct bpf_object *obj,
					    Elf64_Shdr *shdr, Elf_Data *data);

static int bpf_object__collect_map_relos(struct bpf_object *obj,
					 Elf64_Shdr *shdr, Elf_Data *data)
{
	const int bpf_ptr_sz = 8, host_ptr_sz = sizeof(void *);
	int i, j, nrels, new_sz;
	const struct btf_var_secinfo *vi = NULL;
	const struct btf_type *sec, *var, *def;
	struct bpf_map *map = NULL, *targ_map = NULL;
	struct bpf_program *targ_prog = NULL;
	bool is_prog_array, is_map_in_map;
	const struct btf_member *member;
	const char *name, *mname, *type;
	unsigned int moff;
	Elf64_Sym *sym;
	Elf64_Rel *rel;
	void *tmp;

	if (!obj->efile.btf_maps_sec_btf_id || !obj->btf)
		return -EINVAL;
	sec = btf__type_by_id(obj->btf, obj->efile.btf_maps_sec_btf_id);
	if (!sec)
		return -EINVAL;

	nrels = shdr->sh_size / shdr->sh_entsize;
	for (i = 0; i < nrels; i++) {
		rel = elf_rel_by_idx(data, i);
		if (!rel) {
			pr_warn(".maps relo #%d: failed to get ELF relo\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		sym = elf_sym_by_idx(obj, ELF64_R_SYM(rel->r_info));
		if (!sym) {
			pr_warn(".maps relo #%d: symbol %zx not found\n",
				i, (size_t)ELF64_R_SYM(rel->r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}
		name = elf_sym_str(obj, sym->st_name) ?: "<?>";

		pr_debug(".maps relo #%d: for %zd value %zd rel->r_offset %zu name %d ('%s')\n",
			 i, (ssize_t)(rel->r_info >> 32), (size_t)sym->st_value,
			 (size_t)rel->r_offset, sym->st_name, name);

		for (j = 0; j < obj->nr_maps; j++) {
			map = &obj->maps[j];
			if (map->sec_idx != obj->efile.btf_maps_shndx)
				continue;

			vi = btf_var_secinfos(sec) + map->btf_var_idx;
			if (vi->offset <= rel->r_offset &&
			    rel->r_offset + bpf_ptr_sz <= vi->offset + vi->size)
				break;
		}
		if (j == obj->nr_maps) {
			pr_warn(".maps relo #%d: cannot find map '%s' at rel->r_offset %zu\n",
				i, name, (size_t)rel->r_offset);
			return -EINVAL;
		}

		is_map_in_map = bpf_map_type__is_map_in_map(map->def.type);
		is_prog_array = map->def.type == BPF_MAP_TYPE_PROG_ARRAY;
		type = is_map_in_map ? "map" : "prog";
		if (is_map_in_map) {
			if (sym->st_shndx != obj->efile.btf_maps_shndx) {
				pr_warn(".maps relo #%d: '%s' isn't a BTF-defined map\n",
					i, name);
				return -LIBBPF_ERRNO__RELOC;
			}
			if (map->def.type == BPF_MAP_TYPE_HASH_OF_MAPS &&
			    map->def.key_size != sizeof(int)) {
				pr_warn(".maps relo #%d: hash-of-maps '%s' should have key size %zu.\n",
					i, map->name, sizeof(int));
				return -EINVAL;
			}
			targ_map = bpf_object__find_map_by_name(obj, name);
			if (!targ_map) {
				pr_warn(".maps relo #%d: '%s' isn't a valid map reference\n",
					i, name);
				return -ESRCH;
			}
		} else if (is_prog_array) {
			targ_prog = bpf_object__find_program_by_name(obj, name);
			if (!targ_prog) {
				pr_warn(".maps relo #%d: '%s' isn't a valid program reference\n",
					i, name);
				return -ESRCH;
			}
			if (targ_prog->sec_idx != sym->st_shndx ||
			    targ_prog->sec_insn_off * 8 != sym->st_value ||
			    prog_is_subprog(obj, targ_prog)) {
				pr_warn(".maps relo #%d: '%s' isn't an entry-point program\n",
					i, name);
				return -LIBBPF_ERRNO__RELOC;
			}
		} else {
			return -EINVAL;
		}

		var = btf__type_by_id(obj->btf, vi->type);
		def = skip_mods_and_typedefs(obj->btf, var->type, NULL);
		if (btf_vlen(def) == 0)
			return -EINVAL;
		member = btf_members(def) + btf_vlen(def) - 1;
		mname = btf__name_by_offset(obj->btf, member->name_off);
		if (strcmp(mname, "values"))
			return -EINVAL;

		moff = btf_member_bit_offset(def, btf_vlen(def) - 1) / 8;
		if (rel->r_offset - vi->offset < moff)
			return -EINVAL;

		moff = rel->r_offset - vi->offset - moff;
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
		map->init_slots[moff] = is_map_in_map ? (void *)targ_map : (void *)targ_prog;

		pr_debug(".maps relo #%d: map '%s' slot [%d] points to %s '%s'\n",
			 i, map->name, moff, type, name);
	}

	return 0;
}

static int bpf_object__collect_relos(struct bpf_object *obj)
{
	int i, err;

	for (i = 0; i < obj->efile.sec_cnt; i++) {
		struct elf_sec_desc *sec_desc = &obj->efile.secs[i];
		Elf64_Shdr *shdr;
		Elf_Data *data;
		int idx;

		if (sec_desc->sec_type != SEC_RELO)
			continue;

		shdr = sec_desc->shdr;
		data = sec_desc->data;
		idx = shdr->sh_info;

		if (shdr->sh_type != SHT_REL || idx < 0 || idx >= obj->efile.sec_cnt) {
			pr_warn("internal error at %d\n", __LINE__);
			return -LIBBPF_ERRNO__INTERNAL;
		}

		if (obj->efile.secs[idx].sec_type == SEC_ST_OPS)
			err = bpf_object__collect_st_ops_relos(obj, shdr, data);
		else if (idx == obj->efile.btf_maps_shndx)
			err = bpf_object__collect_map_relos(obj, shdr, data);
		else
			err = bpf_object__collect_prog_relos(obj, shdr, data);
		if (err)
			return err;
	}

	bpf_object__sort_relos(obj);
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

static int libbpf_find_attach_btf_id(struct bpf_program *prog, const char *attach_name,
				     int *btf_obj_fd, int *btf_type_id);

/* this is called as prog->sec_def->prog_prepare_load_fn for libbpf-supported sec_defs */
static int libbpf_prepare_prog_load(struct bpf_program *prog,
				    struct bpf_prog_load_opts *opts, long cookie)
{
	enum sec_def_flags def = cookie;

	/* old kernels might not support specifying expected_attach_type */
	if ((def & SEC_EXP_ATTACH_OPT) && !kernel_supports(prog->obj, FEAT_EXP_ATTACH_TYPE))
		opts->expected_attach_type = 0;

	if (def & SEC_SLEEPABLE)
		opts->prog_flags |= BPF_F_SLEEPABLE;

	if (prog->type == BPF_PROG_TYPE_XDP && (def & SEC_XDP_FRAGS))
		opts->prog_flags |= BPF_F_XDP_HAS_FRAGS;

	/* special check for usdt to use uprobe_multi link */
	if ((def & SEC_USDT) && kernel_supports(prog->obj, FEAT_UPROBE_MULTI_LINK))
		prog->expected_attach_type = BPF_TRACE_UPROBE_MULTI;

	if ((def & SEC_ATTACH_BTF) && !prog->attach_btf_id) {
		int btf_obj_fd = 0, btf_type_id = 0, err;
		const char *attach_name;

		attach_name = strchr(prog->sec_name, '/');
		if (!attach_name) {
			/* if BPF program is annotated with just SEC("fentry")
			 * (or similar) without declaratively specifying
			 * target, then it is expected that target will be
			 * specified with bpf_program__set_attach_target() at
			 * runtime before BPF object load step. If not, then
			 * there is nothing to load into the kernel as BPF
			 * verifier won't be able to validate BPF program
			 * correctness anyways.
			 */
			pr_warn("prog '%s': no BTF-based attach target is specified, use bpf_program__set_attach_target()\n",
				prog->name);
			return -EINVAL;
		}
		attach_name++; /* skip over / */

		err = libbpf_find_attach_btf_id(prog, attach_name, &btf_obj_fd, &btf_type_id);
		if (err)
			return err;

		/* cache resolved BTF FD and BTF type ID in the prog */
		prog->attach_btf_obj_fd = btf_obj_fd;
		prog->attach_btf_id = btf_type_id;

		/* but by now libbpf common logic is not utilizing
		 * prog->atach_btf_obj_fd/prog->attach_btf_id anymore because
		 * this callback is called after opts were populated by
		 * libbpf, so this callback has to update opts explicitly here
		 */
		opts->attach_btf_obj_fd = btf_obj_fd;
		opts->attach_btf_id = btf_type_id;
	}
	return 0;
}

static void fixup_verifier_log(struct bpf_program *prog, char *buf, size_t buf_sz);

static int bpf_object_load_prog(struct bpf_object *obj, struct bpf_program *prog,
				struct bpf_insn *insns, int insns_cnt,
				const char *license, __u32 kern_version, int *prog_fd)
{
	LIBBPF_OPTS(bpf_prog_load_opts, load_attr);
	const char *prog_name = NULL;
	char *cp, errmsg[STRERR_BUFSIZE];
	size_t log_buf_size = 0;
	char *log_buf = NULL, *tmp;
	bool own_log_buf = true;
	__u32 log_level = prog->log_level;
	int ret, err;

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

	if (kernel_supports(obj, FEAT_PROG_NAME))
		prog_name = prog->name;
	load_attr.attach_prog_fd = prog->attach_prog_fd;
	load_attr.attach_btf_obj_fd = prog->attach_btf_obj_fd;
	load_attr.attach_btf_id = prog->attach_btf_id;
	load_attr.kern_version = kern_version;
	load_attr.prog_ifindex = prog->prog_ifindex;

	/* specify func_info/line_info only if kernel supports them */
	if (obj->btf && btf__fd(obj->btf) >= 0 && kernel_supports(obj, FEAT_BTF_FUNC)) {
		load_attr.prog_btf_fd = btf__fd(obj->btf);
		load_attr.func_info = prog->func_info;
		load_attr.func_info_rec_size = prog->func_info_rec_size;
		load_attr.func_info_cnt = prog->func_info_cnt;
		load_attr.line_info = prog->line_info;
		load_attr.line_info_rec_size = prog->line_info_rec_size;
		load_attr.line_info_cnt = prog->line_info_cnt;
	}
	load_attr.log_level = log_level;
	load_attr.prog_flags = prog->prog_flags;
	load_attr.fd_array = obj->fd_array;

	load_attr.token_fd = obj->token_fd;
	if (obj->token_fd)
		load_attr.prog_flags |= BPF_F_TOKEN_FD;

	/* adjust load_attr if sec_def provides custom preload callback */
	if (prog->sec_def && prog->sec_def->prog_prepare_load_fn) {
		err = prog->sec_def->prog_prepare_load_fn(prog, &load_attr, prog->sec_def->cookie);
		if (err < 0) {
			pr_warn("prog '%s': failed to prepare load attributes: %d\n",
				prog->name, err);
			return err;
		}
		insns = prog->insns;
		insns_cnt = prog->insns_cnt;
	}

	/* allow prog_prepare_load_fn to change expected_attach_type */
	load_attr.expected_attach_type = prog->expected_attach_type;

	if (obj->gen_loader) {
		bpf_gen__prog_load(obj->gen_loader, prog->type, prog->name,
				   license, insns, insns_cnt, &load_attr,
				   prog - obj->programs);
		*prog_fd = -1;
		return 0;
	}

retry_load:
	/* if log_level is zero, we don't request logs initially even if
	 * custom log_buf is specified; if the program load fails, then we'll
	 * bump log_level to 1 and use either custom log_buf or we'll allocate
	 * our own and retry the load to get details on what failed
	 */
	if (log_level) {
		if (prog->log_buf) {
			log_buf = prog->log_buf;
			log_buf_size = prog->log_size;
			own_log_buf = false;
		} else if (obj->log_buf) {
			log_buf = obj->log_buf;
			log_buf_size = obj->log_size;
			own_log_buf = false;
		} else {
			log_buf_size = max((size_t)BPF_LOG_BUF_SIZE, log_buf_size * 2);
			tmp = realloc(log_buf, log_buf_size);
			if (!tmp) {
				ret = -ENOMEM;
				goto out;
			}
			log_buf = tmp;
			log_buf[0] = '\0';
			own_log_buf = true;
		}
	}

	load_attr.log_buf = log_buf;
	load_attr.log_size = log_buf_size;
	load_attr.log_level = log_level;

	ret = bpf_prog_load(prog->type, prog_name, license, insns, insns_cnt, &load_attr);
	if (ret >= 0) {
		if (log_level && own_log_buf) {
			pr_debug("prog '%s': -- BEGIN PROG LOAD LOG --\n%s-- END PROG LOAD LOG --\n",
				 prog->name, log_buf);
		}

		if (obj->has_rodata && kernel_supports(obj, FEAT_PROG_BIND_MAP)) {
			struct bpf_map *map;
			int i;

			for (i = 0; i < obj->nr_maps; i++) {
				map = &prog->obj->maps[i];
				if (map->libbpf_type != LIBBPF_MAP_RODATA)
					continue;

				if (bpf_prog_bind_map(ret, map->fd, NULL)) {
					cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
					pr_warn("prog '%s': failed to bind map '%s': %s\n",
						prog->name, map->real_name, cp);
					/* Don't fail hard if can't bind rodata. */
				}
			}
		}

		*prog_fd = ret;
		ret = 0;
		goto out;
	}

	if (log_level == 0) {
		log_level = 1;
		goto retry_load;
	}
	/* On ENOSPC, increase log buffer size and retry, unless custom
	 * log_buf is specified.
	 * Be careful to not overflow u32, though. Kernel's log buf size limit
	 * isn't part of UAPI so it can always be bumped to full 4GB. So don't
	 * multiply by 2 unless we are sure we'll fit within 32 bits.
	 * Currently, we'll get -EINVAL when we reach (UINT_MAX >> 2).
	 */
	if (own_log_buf && errno == ENOSPC && log_buf_size <= UINT_MAX / 2)
		goto retry_load;

	ret = -errno;

	/* post-process verifier log to improve error descriptions */
	fixup_verifier_log(prog, log_buf, log_buf_size);

	cp = libbpf_strerror_r(errno, errmsg, sizeof(errmsg));
	pr_warn("prog '%s': BPF program load failed: %s\n", prog->name, cp);
	pr_perm_msg(ret);

	if (own_log_buf && log_buf && log_buf[0] != '\0') {
		pr_warn("prog '%s': -- BEGIN PROG LOAD LOG --\n%s-- END PROG LOAD LOG --\n",
			prog->name, log_buf);
	}

out:
	if (own_log_buf)
		free(log_buf);
	return ret;
}

static char *find_prev_line(char *buf, char *cur)
{
	char *p;

	if (cur == buf) /* end of a log buf */
		return NULL;

	p = cur - 1;
	while (p - 1 >= buf && *(p - 1) != '\n')
		p--;

	return p;
}

static void patch_log(char *buf, size_t buf_sz, size_t log_sz,
		      char *orig, size_t orig_sz, const char *patch)
{
	/* size of the remaining log content to the right from the to-be-replaced part */
	size_t rem_sz = (buf + log_sz) - (orig + orig_sz);
	size_t patch_sz = strlen(patch);

	if (patch_sz != orig_sz) {
		/* If patch line(s) are longer than original piece of verifier log,
		 * shift log contents by (patch_sz - orig_sz) bytes to the right
		 * starting from after to-be-replaced part of the log.
		 *
		 * If patch line(s) are shorter than original piece of verifier log,
		 * shift log contents by (orig_sz - patch_sz) bytes to the left
		 * starting from after to-be-replaced part of the log
		 *
		 * We need to be careful about not overflowing available
		 * buf_sz capacity. If that's the case, we'll truncate the end
		 * of the original log, as necessary.
		 */
		if (patch_sz > orig_sz) {
			if (orig + patch_sz >= buf + buf_sz) {
				/* patch is big enough to cover remaining space completely */
				patch_sz -= (orig + patch_sz) - (buf + buf_sz) + 1;
				rem_sz = 0;
			} else if (patch_sz - orig_sz > buf_sz - log_sz) {
				/* patch causes part of remaining log to be truncated */
				rem_sz -= (patch_sz - orig_sz) - (buf_sz - log_sz);
			}
		}
		/* shift remaining log to the right by calculated amount */
		memmove(orig + patch_sz, orig + orig_sz, rem_sz);
	}

	memcpy(orig, patch, patch_sz);
}

static void fixup_log_failed_core_relo(struct bpf_program *prog,
				       char *buf, size_t buf_sz, size_t log_sz,
				       char *line1, char *line2, char *line3)
{
	/* Expected log for failed and not properly guarded CO-RE relocation:
	 * line1 -> 123: (85) call unknown#195896080
	 * line2 -> invalid func unknown#195896080
	 * line3 -> <anything else or end of buffer>
	 *
	 * "123" is the index of the instruction that was poisoned. We extract
	 * instruction index to find corresponding CO-RE relocation and
	 * replace this part of the log with more relevant information about
	 * failed CO-RE relocation.
	 */
	const struct bpf_core_relo *relo;
	struct bpf_core_spec spec;
	char patch[512], spec_buf[256];
	int insn_idx, err, spec_len;

	if (sscanf(line1, "%d: (%*d) call unknown#195896080\n", &insn_idx) != 1)
		return;

	relo = find_relo_core(prog, insn_idx);
	if (!relo)
		return;

	err = bpf_core_parse_spec(prog->name, prog->obj->btf, relo, &spec);
	if (err)
		return;

	spec_len = bpf_core_format_spec(spec_buf, sizeof(spec_buf), &spec);
	snprintf(patch, sizeof(patch),
		 "%d: <invalid CO-RE relocation>\n"
		 "failed to resolve CO-RE relocation %s%s\n",
		 insn_idx, spec_buf, spec_len >= sizeof(spec_buf) ? "..." : "");

	patch_log(buf, buf_sz, log_sz, line1, line3 - line1, patch);
}

static void fixup_log_missing_map_load(struct bpf_program *prog,
				       char *buf, size_t buf_sz, size_t log_sz,
				       char *line1, char *line2, char *line3)
{
	/* Expected log for failed and not properly guarded map reference:
	 * line1 -> 123: (85) call unknown#2001000345
	 * line2 -> invalid func unknown#2001000345
	 * line3 -> <anything else or end of buffer>
	 *
	 * "123" is the index of the instruction that was poisoned.
	 * "345" in "2001000345" is a map index in obj->maps to fetch map name.
	 */
	struct bpf_object *obj = prog->obj;
	const struct bpf_map *map;
	int insn_idx, map_idx;
	char patch[128];

	if (sscanf(line1, "%d: (%*d) call unknown#%d\n", &insn_idx, &map_idx) != 2)
		return;

	map_idx -= POISON_LDIMM64_MAP_BASE;
	if (map_idx < 0 || map_idx >= obj->nr_maps)
		return;
	map = &obj->maps[map_idx];

	snprintf(patch, sizeof(patch),
		 "%d: <invalid BPF map reference>\n"
		 "BPF map '%s' is referenced but wasn't created\n",
		 insn_idx, map->name);

	patch_log(buf, buf_sz, log_sz, line1, line3 - line1, patch);
}

static void fixup_log_missing_kfunc_call(struct bpf_program *prog,
					 char *buf, size_t buf_sz, size_t log_sz,
					 char *line1, char *line2, char *line3)
{
	/* Expected log for failed and not properly guarded kfunc call:
	 * line1 -> 123: (85) call unknown#2002000345
	 * line2 -> invalid func unknown#2002000345
	 * line3 -> <anything else or end of buffer>
	 *
	 * "123" is the index of the instruction that was poisoned.
	 * "345" in "2002000345" is an extern index in obj->externs to fetch kfunc name.
	 */
	struct bpf_object *obj = prog->obj;
	const struct extern_desc *ext;
	int insn_idx, ext_idx;
	char patch[128];

	if (sscanf(line1, "%d: (%*d) call unknown#%d\n", &insn_idx, &ext_idx) != 2)
		return;

	ext_idx -= POISON_CALL_KFUNC_BASE;
	if (ext_idx < 0 || ext_idx >= obj->nr_extern)
		return;
	ext = &obj->externs[ext_idx];

	snprintf(patch, sizeof(patch),
		 "%d: <invalid kfunc call>\n"
		 "kfunc '%s' is referenced but wasn't resolved\n",
		 insn_idx, ext->name);

	patch_log(buf, buf_sz, log_sz, line1, line3 - line1, patch);
}

static void fixup_verifier_log(struct bpf_program *prog, char *buf, size_t buf_sz)
{
	/* look for familiar error patterns in last N lines of the log */
	const size_t max_last_line_cnt = 10;
	char *prev_line, *cur_line, *next_line;
	size_t log_sz;
	int i;

	if (!buf)
		return;

	log_sz = strlen(buf) + 1;
	next_line = buf + log_sz - 1;

	for (i = 0; i < max_last_line_cnt; i++, next_line = cur_line) {
		cur_line = find_prev_line(buf, next_line);
		if (!cur_line)
			return;

		if (str_has_pfx(cur_line, "invalid func unknown#195896080\n")) {
			prev_line = find_prev_line(buf, cur_line);
			if (!prev_line)
				continue;

			/* failed CO-RE relocation case */
			fixup_log_failed_core_relo(prog, buf, buf_sz, log_sz,
						   prev_line, cur_line, next_line);
			return;
		} else if (str_has_pfx(cur_line, "invalid func unknown#"POISON_LDIMM64_MAP_PFX)) {
			prev_line = find_prev_line(buf, cur_line);
			if (!prev_line)
				continue;

			/* reference to uncreated BPF map */
			fixup_log_missing_map_load(prog, buf, buf_sz, log_sz,
						   prev_line, cur_line, next_line);
			return;
		} else if (str_has_pfx(cur_line, "invalid func unknown#"POISON_CALL_KFUNC_PFX)) {
			prev_line = find_prev_line(buf, cur_line);
			if (!prev_line)
				continue;

			/* reference to unresolved kfunc */
			fixup_log_missing_kfunc_call(prog, buf, buf_sz, log_sz,
						     prev_line, cur_line, next_line);
			return;
		}
	}
}

static int bpf_program_record_relos(struct bpf_program *prog)
{
	struct bpf_object *obj = prog->obj;
	int i;

	for (i = 0; i < prog->nr_reloc; i++) {
		struct reloc_desc *relo = &prog->reloc_desc[i];
		struct extern_desc *ext = &obj->externs[relo->ext_idx];
		int kind;

		switch (relo->type) {
		case RELO_EXTERN_LD64:
			if (ext->type != EXT_KSYM)
				continue;
			kind = btf_is_var(btf__type_by_id(obj->btf, ext->btf_id)) ?
				BTF_KIND_VAR : BTF_KIND_FUNC;
			bpf_gen__record_extern(obj->gen_loader, ext->name,
					       ext->is_weak, !ext->ksym.type_id,
					       true, kind, relo->insn_idx);
			break;
		case RELO_EXTERN_CALL:
			bpf_gen__record_extern(obj->gen_loader, ext->name,
					       ext->is_weak, false, false, BTF_KIND_FUNC,
					       relo->insn_idx);
			break;
		case RELO_CORE: {
			struct bpf_core_relo cr = {
				.insn_off = relo->insn_idx * 8,
				.type_id = relo->core_relo->type_id,
				.access_str_off = relo->core_relo->access_str_off,
				.kind = relo->core_relo->kind,
			};

			bpf_gen__record_relo_core(obj->gen_loader, &cr);
			break;
		}
		default:
			continue;
		}
	}
	return 0;
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
		if (!prog->autoload) {
			pr_debug("prog '%s': skipped loading\n", prog->name);
			continue;
		}
		prog->log_level |= log_level;

		if (obj->gen_loader)
			bpf_program_record_relos(prog);

		err = bpf_object_load_prog(obj, prog, prog->insns, prog->insns_cnt,
					   obj->license, obj->kern_version, &prog->fd);
		if (err) {
			pr_warn("prog '%s': failed to load: %d\n", prog->name, err);
			return err;
		}
	}

	bpf_object__free_relocs(obj);
	return 0;
}

static const struct bpf_sec_def *find_sec_def(const char *sec_name);

static int bpf_object_init_progs(struct bpf_object *obj, const struct bpf_object_open_opts *opts)
{
	struct bpf_program *prog;
	int err;

	bpf_object__for_each_program(prog, obj) {
		prog->sec_def = find_sec_def(prog->sec_name);
		if (!prog->sec_def) {
			/* couldn't guess, but user might manually specify */
			pr_debug("prog '%s': unrecognized ELF section name '%s'\n",
				prog->name, prog->sec_name);
			continue;
		}

		prog->type = prog->sec_def->prog_type;
		prog->expected_attach_type = prog->sec_def->expected_attach_type;

		/* sec_def can have custom callback which should be called
		 * after bpf_program is initialized to adjust its properties
		 */
		if (prog->sec_def->prog_setup_fn) {
			err = prog->sec_def->prog_setup_fn(prog, prog->sec_def->cookie);
			if (err < 0) {
				pr_warn("prog '%s': failed to initialize: %d\n",
					prog->name, err);
				return err;
			}
		}
	}

	return 0;
}

static struct bpf_object *bpf_object_open(const char *path, const void *obj_buf, size_t obj_buf_sz,
					  const struct bpf_object_open_opts *opts)
{
	const char *obj_name, *kconfig, *btf_tmp_path, *token_path;
	struct bpf_object *obj;
	char tmp_name[64];
	int err;
	char *log_buf;
	size_t log_size;
	__u32 log_level;

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

	log_buf = OPTS_GET(opts, kernel_log_buf, NULL);
	log_size = OPTS_GET(opts, kernel_log_size, 0);
	log_level = OPTS_GET(opts, kernel_log_level, 0);
	if (log_size > UINT_MAX)
		return ERR_PTR(-EINVAL);
	if (log_size && !log_buf)
		return ERR_PTR(-EINVAL);

	token_path = OPTS_GET(opts, bpf_token_path, NULL);
	/* if user didn't specify bpf_token_path explicitly, check if
	 * LIBBPF_BPF_TOKEN_PATH envvar was set and treat it as bpf_token_path
	 * option
	 */
	if (!token_path)
		token_path = getenv("LIBBPF_BPF_TOKEN_PATH");
	if (token_path && strlen(token_path) >= PATH_MAX)
		return ERR_PTR(-ENAMETOOLONG);

	obj = bpf_object__new(path, obj_buf, obj_buf_sz, obj_name);
	if (IS_ERR(obj))
		return obj;

	obj->log_buf = log_buf;
	obj->log_size = log_size;
	obj->log_level = log_level;

	if (token_path) {
		obj->token_path = strdup(token_path);
		if (!obj->token_path) {
			err = -ENOMEM;
			goto out;
		}
	}

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
	err = err ? : bpf_object_fixup_btf(obj);
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

struct bpf_object *
bpf_object__open_file(const char *path, const struct bpf_object_open_opts *opts)
{
	if (!path)
		return libbpf_err_ptr(-EINVAL);

	pr_debug("loading %s\n", path);

	return libbpf_ptr(bpf_object_open(path, NULL, 0, opts));
}

struct bpf_object *bpf_object__open(const char *path)
{
	return bpf_object__open_file(path, NULL);
}

struct bpf_object *
bpf_object__open_mem(const void *obj_buf, size_t obj_buf_sz,
		     const struct bpf_object_open_opts *opts)
{
	if (!obj_buf || obj_buf_sz == 0)
		return libbpf_err_ptr(-EINVAL);

	return libbpf_ptr(bpf_object_open(NULL, obj_buf, obj_buf_sz, opts));
}

static int bpf_object_unload(struct bpf_object *obj)
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
		if (!kernel_supports(obj, FEAT_ARRAY_MMAP))
			m->def.map_flags &= ~BPF_F_MMAPABLE;
	}

	return 0;
}

typedef int (*kallsyms_cb_t)(unsigned long long sym_addr, char sym_type,
			     const char *sym_name, void *ctx);

static int libbpf_kallsyms_parse(kallsyms_cb_t cb, void *ctx)
{
	char sym_type, sym_name[500];
	unsigned long long sym_addr;
	int ret, err = 0;
	FILE *f;

	f = fopen("/proc/kallsyms", "re");
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
			break;
		}

		err = cb(sym_addr, sym_type, sym_name, ctx);
		if (err)
			break;
	}

	fclose(f);
	return err;
}

static int kallsyms_cb(unsigned long long sym_addr, char sym_type,
		       const char *sym_name, void *ctx)
{
	struct bpf_object *obj = ctx;
	const struct btf_type *t;
	struct extern_desc *ext;
	char *res;

	res = strstr(sym_name, ".llvm.");
	if (sym_type == 'd' && res)
		ext = find_extern_by_name_with_len(obj, sym_name, res - sym_name);
	else
		ext = find_extern_by_name(obj, sym_name);
	if (!ext || ext->type != EXT_KSYM)
		return 0;

	t = btf__type_by_id(obj->btf, ext->btf_id);
	if (!btf_is_var(t))
		return 0;

	if (ext->is_set && ext->ksym.addr != sym_addr) {
		pr_warn("extern (ksym) '%s': resolution is ambiguous: 0x%llx or 0x%llx\n",
			sym_name, ext->ksym.addr, sym_addr);
		return -EINVAL;
	}
	if (!ext->is_set) {
		ext->is_set = true;
		ext->ksym.addr = sym_addr;
		pr_debug("extern (ksym) '%s': set to 0x%llx\n", sym_name, sym_addr);
	}
	return 0;
}

static int bpf_object__read_kallsyms_file(struct bpf_object *obj)
{
	return libbpf_kallsyms_parse(kallsyms_cb, obj);
}

static int find_ksym_btf_id(struct bpf_object *obj, const char *ksym_name,
			    __u16 kind, struct btf **res_btf,
			    struct module_btf **res_mod_btf)
{
	struct module_btf *mod_btf;
	struct btf *btf;
	int i, id, err;

	btf = obj->btf_vmlinux;
	mod_btf = NULL;
	id = btf__find_by_name_kind(btf, ksym_name, kind);

	if (id == -ENOENT) {
		err = load_module_btfs(obj);
		if (err)
			return err;

		for (i = 0; i < obj->btf_module_cnt; i++) {
			/* we assume module_btf's BTF FD is always >0 */
			mod_btf = &obj->btf_modules[i];
			btf = mod_btf->btf;
			id = btf__find_by_name_kind_own(btf, ksym_name, kind);
			if (id != -ENOENT)
				break;
		}
	}
	if (id <= 0)
		return -ESRCH;

	*res_btf = btf;
	*res_mod_btf = mod_btf;
	return id;
}

static int bpf_object__resolve_ksym_var_btf_id(struct bpf_object *obj,
					       struct extern_desc *ext)
{
	const struct btf_type *targ_var, *targ_type;
	__u32 targ_type_id, local_type_id;
	struct module_btf *mod_btf = NULL;
	const char *targ_var_name;
	struct btf *btf = NULL;
	int id, err;

	id = find_ksym_btf_id(obj, ext->name, BTF_KIND_VAR, &btf, &mod_btf);
	if (id < 0) {
		if (id == -ESRCH && ext->is_weak)
			return 0;
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
	ext->ksym.kernel_btf_obj_fd = mod_btf ? mod_btf->fd : 0;
	ext->ksym.kernel_btf_id = id;
	pr_debug("extern (var ksym) '%s': resolved to [%d] %s %s\n",
		 ext->name, id, btf_kind_str(targ_var), targ_var_name);

	return 0;
}

static int bpf_object__resolve_ksym_func_btf_id(struct bpf_object *obj,
						struct extern_desc *ext)
{
	int local_func_proto_id, kfunc_proto_id, kfunc_id;
	struct module_btf *mod_btf = NULL;
	const struct btf_type *kern_func;
	struct btf *kern_btf = NULL;
	int ret;

	local_func_proto_id = ext->ksym.type_id;

	kfunc_id = find_ksym_btf_id(obj, ext->essent_name ?: ext->name, BTF_KIND_FUNC, &kern_btf,
				    &mod_btf);
	if (kfunc_id < 0) {
		if (kfunc_id == -ESRCH && ext->is_weak)
			return 0;
		pr_warn("extern (func ksym) '%s': not found in kernel or module BTFs\n",
			ext->name);
		return kfunc_id;
	}

	kern_func = btf__type_by_id(kern_btf, kfunc_id);
	kfunc_proto_id = kern_func->type;

	ret = bpf_core_types_are_compat(obj->btf, local_func_proto_id,
					kern_btf, kfunc_proto_id);
	if (ret <= 0) {
		if (ext->is_weak)
			return 0;

		pr_warn("extern (func ksym) '%s': func_proto [%d] incompatible with %s [%d]\n",
			ext->name, local_func_proto_id,
			mod_btf ? mod_btf->name : "vmlinux", kfunc_proto_id);
		return -EINVAL;
	}

	/* set index for module BTF fd in fd_array, if unset */
	if (mod_btf && !mod_btf->fd_array_idx) {
		/* insn->off is s16 */
		if (obj->fd_array_cnt == INT16_MAX) {
			pr_warn("extern (func ksym) '%s': module BTF fd index %d too big to fit in bpf_insn offset\n",
				ext->name, mod_btf->fd_array_idx);
			return -E2BIG;
		}
		/* Cannot use index 0 for module BTF fd */
		if (!obj->fd_array_cnt)
			obj->fd_array_cnt = 1;

		ret = libbpf_ensure_mem((void **)&obj->fd_array, &obj->fd_array_cap, sizeof(int),
					obj->fd_array_cnt + 1);
		if (ret)
			return ret;
		mod_btf->fd_array_idx = obj->fd_array_cnt;
		/* we assume module BTF FD is always >0 */
		obj->fd_array[obj->fd_array_cnt++] = mod_btf->fd;
	}

	ext->is_set = true;
	ext->ksym.kernel_btf_id = kfunc_id;
	ext->ksym.btf_fd_idx = mod_btf ? mod_btf->fd_array_idx : 0;
	/* Also set kernel_btf_obj_fd to make sure that bpf_object__relocate_data()
	 * populates FD into ld_imm64 insn when it's used to point to kfunc.
	 * {kernel_btf_id, btf_fd_idx} -> fixup bpf_call.
	 * {kernel_btf_id, kernel_btf_obj_fd} -> fixup ld_imm64.
	 */
	ext->ksym.kernel_btf_obj_fd = mod_btf ? mod_btf->fd : 0;
	pr_debug("extern (func ksym) '%s': resolved to %s [%d]\n",
		 ext->name, mod_btf ? mod_btf->name : "vmlinux", kfunc_id);

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

		if (ext->type == EXT_KSYM) {
			if (ext->ksym.type_id)
				need_vmlinux_btf = true;
			else
				need_kallsyms = true;
			continue;
		} else if (ext->type == EXT_KCFG) {
			void *ext_ptr = kcfg_data + ext->kcfg.data_off;
			__u64 value = 0;

			/* Kconfig externs need actual /proc/config.gz */
			if (str_has_pfx(ext->name, "CONFIG_")) {
				need_config = true;
				continue;
			}

			/* Virtual kcfg externs are customly handled by libbpf */
			if (strcmp(ext->name, "LINUX_KERNEL_VERSION") == 0) {
				value = get_kernel_version();
				if (!value) {
					pr_warn("extern (kcfg) '%s': failed to get kernel version\n", ext->name);
					return -EINVAL;
				}
			} else if (strcmp(ext->name, "LINUX_HAS_BPF_COOKIE") == 0) {
				value = kernel_supports(obj, FEAT_BPF_COOKIE);
			} else if (strcmp(ext->name, "LINUX_HAS_SYSCALL_WRAPPER") == 0) {
				value = kernel_supports(obj, FEAT_SYSCALL_WRAPPER);
			} else if (!str_has_pfx(ext->name, "LINUX_") || !ext->is_weak) {
				/* Currently libbpf supports only CONFIG_ and LINUX_ prefixed
				 * __kconfig externs, where LINUX_ ones are virtual and filled out
				 * customly by libbpf (their values don't come from Kconfig).
				 * If LINUX_xxx variable is not recognized by libbpf, but is marked
				 * __weak, it defaults to zero value, just like for CONFIG_xxx
				 * externs.
				 */
				pr_warn("extern (kcfg) '%s': unrecognized virtual extern\n", ext->name);
				return -EINVAL;
			}

			err = set_kcfg_value_num(ext, ext_ptr, value);
			if (err)
				return err;
			pr_debug("extern (kcfg) '%s': set to 0x%llx\n",
				 ext->name, (long long)value);
		} else {
			pr_warn("extern '%s': unrecognized extern kind\n", ext->name);
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
			pr_warn("extern '%s' (strong): not resolved\n", ext->name);
			return -ESRCH;
		} else if (!ext->is_set) {
			pr_debug("extern '%s' (weak): not resolved, defaulting to zero\n",
				 ext->name);
		}
	}

	return 0;
}

static void bpf_map_prepare_vdata(const struct bpf_map *map)
{
	struct bpf_struct_ops *st_ops;
	__u32 i;

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
}

static int bpf_object_prepare_struct_ops(struct bpf_object *obj)
{
	struct bpf_map *map;
	int i;

	for (i = 0; i < obj->nr_maps; i++) {
		map = &obj->maps[i];

		if (!bpf_map__is_struct_ops(map))
			continue;

		if (!map->autocreate)
			continue;

		bpf_map_prepare_vdata(map);
	}

	return 0;
}

static int bpf_object_load(struct bpf_object *obj, int extra_log_level, const char *target_btf_path)
{
	int err, i;

	if (!obj)
		return libbpf_err(-EINVAL);

	if (obj->loaded) {
		pr_warn("object '%s': load can't be attempted twice\n", obj->name);
		return libbpf_err(-EINVAL);
	}

	if (obj->gen_loader)
		bpf_gen__init(obj->gen_loader, extra_log_level, obj->nr_programs, obj->nr_maps);

	err = bpf_object_prepare_token(obj);
	err = err ? : bpf_object__probe_loading(obj);
	err = err ? : bpf_object__load_vmlinux_btf(obj, false);
	err = err ? : bpf_object__resolve_externs(obj, obj->kconfig);
	err = err ? : bpf_object__sanitize_maps(obj);
	err = err ? : bpf_object__init_kern_struct_ops_maps(obj);
	err = err ? : bpf_object_adjust_struct_ops_autoload(obj);
	err = err ? : bpf_object__relocate(obj, obj->btf_custom_path ? : target_btf_path);
	err = err ? : bpf_object__sanitize_and_load_btf(obj);
	err = err ? : bpf_object__create_maps(obj);
	err = err ? : bpf_object__load_progs(obj, extra_log_level);
	err = err ? : bpf_object_init_prog_arrays(obj);
	err = err ? : bpf_object_prepare_struct_ops(obj);

	if (obj->gen_loader) {
		/* reset FDs */
		if (obj->btf)
			btf__set_fd(obj->btf, -1);
		if (!err)
			err = bpf_gen__finish(obj->gen_loader, obj->nr_programs, obj->nr_maps);
	}

	/* clean up fd_array */
	zfree(&obj->fd_array);

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

	bpf_object_unload(obj);
	pr_warn("failed to load object '%s'\n", obj->path);
	return libbpf_err(err);
}

int bpf_object__load(struct bpf_object *obj)
{
	return bpf_object_load(obj, 0, NULL);
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

int bpf_program__pin(struct bpf_program *prog, const char *path)
{
	char *cp, errmsg[STRERR_BUFSIZE];
	int err;

	if (prog->fd < 0) {
		pr_warn("prog '%s': can't pin program that wasn't loaded\n", prog->name);
		return libbpf_err(-EINVAL);
	}

	err = make_parent_dir(path);
	if (err)
		return libbpf_err(err);

	err = check_path(path);
	if (err)
		return libbpf_err(err);

	if (bpf_obj_pin(prog->fd, path)) {
		err = -errno;
		cp = libbpf_strerror_r(err, errmsg, sizeof(errmsg));
		pr_warn("prog '%s': failed to pin at '%s': %s\n", prog->name, path, cp);
		return libbpf_err(err);
	}

	pr_debug("prog '%s': pinned at '%s'\n", prog->name, path);
	return 0;
}

int bpf_program__unpin(struct bpf_program *prog, const char *path)
{
	int err;

	if (prog->fd < 0) {
		pr_warn("prog '%s': can't unpin program that wasn't loaded\n", prog->name);
		return libbpf_err(-EINVAL);
	}

	err = check_path(path);
	if (err)
		return libbpf_err(err);

	err = unlink(path);
	if (err)
		return libbpf_err(-errno);

	pr_debug("prog '%s': unpinned from '%s'\n", prog->name, path);
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

	if (map->fd < 0) {
		pr_warn("map '%s': can't pin BPF map without FD (was it created?)\n", map->name);
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

__alias(bpf_map__pin_path)
const char *bpf_map__get_pin_path(const struct bpf_map *map);

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

		if (!map->autocreate)
			continue;

		if (path) {
			err = pathname_concat(buf, sizeof(buf), path, bpf_map__name(map));
			if (err)
				goto err_unpin_maps;
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
	while ((map = bpf_object__prev_map(obj, map))) {
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
			err = pathname_concat(buf, sizeof(buf), path, bpf_map__name(map));
			if (err)
				return libbpf_err(err);
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
	char buf[PATH_MAX];
	int err;

	if (!obj)
		return libbpf_err(-ENOENT);

	if (!obj->loaded) {
		pr_warn("object not yet loaded; load it first\n");
		return libbpf_err(-ENOENT);
	}

	bpf_object__for_each_program(prog, obj) {
		err = pathname_concat(buf, sizeof(buf), path, prog->name);
		if (err)
			goto err_unpin_programs;

		err = bpf_program__pin(prog, buf);
		if (err)
			goto err_unpin_programs;
	}

	return 0;

err_unpin_programs:
	while ((prog = bpf_object__prev_program(obj, prog))) {
		if (pathname_concat(buf, sizeof(buf), path, prog->name))
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

		err = pathname_concat(buf, sizeof(buf), path, prog->name);
		if (err)
			return libbpf_err(err);

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

int bpf_object__unpin(struct bpf_object *obj, const char *path)
{
	int err;

	err = bpf_object__unpin_programs(obj, path);
	if (err)
		return libbpf_err(err);

	err = bpf_object__unpin_maps(obj, path);
	if (err)
		return libbpf_err(err);

	return 0;
}

static void bpf_map__destroy(struct bpf_map *map)
{
	if (map->inner_map) {
		bpf_map__destroy(map->inner_map);
		zfree(&map->inner_map);
	}

	zfree(&map->init_slots);
	map->init_slots_sz = 0;

	if (map->mmaped && map->mmaped != map->obj->arena_data)
		munmap(map->mmaped, bpf_map_mmap_sz(map));
	map->mmaped = NULL;

	if (map->st_ops) {
		zfree(&map->st_ops->data);
		zfree(&map->st_ops->progs);
		zfree(&map->st_ops->kern_func_off);
		zfree(&map->st_ops);
	}

	zfree(&map->name);
	zfree(&map->real_name);
	zfree(&map->pin_path);

	if (map->fd >= 0)
		zclose(map->fd);
}

void bpf_object__close(struct bpf_object *obj)
{
	size_t i;

	if (IS_ERR_OR_NULL(obj))
		return;

	usdt_manager_free(obj->usdt_man);
	obj->usdt_man = NULL;

	bpf_gen__free(obj->gen_loader);
	bpf_object__elf_finish(obj);
	bpf_object_unload(obj);
	btf__free(obj->btf);
	btf__free(obj->btf_vmlinux);
	btf_ext__free(obj->btf_ext);

	for (i = 0; i < obj->nr_maps; i++)
		bpf_map__destroy(&obj->maps[i]);

	zfree(&obj->btf_custom_path);
	zfree(&obj->kconfig);

	for (i = 0; i < obj->nr_extern; i++)
		zfree(&obj->externs[i].essent_name);

	zfree(&obj->externs);
	obj->nr_extern = 0;

	zfree(&obj->maps);
	obj->nr_maps = 0;

	if (obj->programs && obj->nr_programs) {
		for (i = 0; i < obj->nr_programs; i++)
			bpf_program__exit(&obj->programs[i]);
	}
	zfree(&obj->programs);

	zfree(&obj->feat_cache);
	zfree(&obj->token_path);
	if (obj->token_fd > 0)
		close(obj->token_fd);

	zfree(&obj->arena_data);

	free(obj);
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
bpf_object__next_program(const struct bpf_object *obj, struct bpf_program *prev)
{
	struct bpf_program *prog = prev;

	do {
		prog = __bpf_program__iter(prog, obj, true);
	} while (prog && prog_is_subprog(obj, prog));

	return prog;
}

struct bpf_program *
bpf_object__prev_program(const struct bpf_object *obj, struct bpf_program *next)
{
	struct bpf_program *prog = next;

	do {
		prog = __bpf_program__iter(prog, obj, false);
	} while (prog && prog_is_subprog(obj, prog));

	return prog;
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

bool bpf_program__autoload(const struct bpf_program *prog)
{
	return prog->autoload;
}

int bpf_program__set_autoload(struct bpf_program *prog, bool autoload)
{
	if (prog->obj->loaded)
		return libbpf_err(-EINVAL);

	prog->autoload = autoload;
	return 0;
}

bool bpf_program__autoattach(const struct bpf_program *prog)
{
	return prog->autoattach;
}

void bpf_program__set_autoattach(struct bpf_program *prog, bool autoattach)
{
	prog->autoattach = autoattach;
}

const struct bpf_insn *bpf_program__insns(const struct bpf_program *prog)
{
	return prog->insns;
}

size_t bpf_program__insn_cnt(const struct bpf_program *prog)
{
	return prog->insns_cnt;
}

int bpf_program__set_insns(struct bpf_program *prog,
			   struct bpf_insn *new_insns, size_t new_insn_cnt)
{
	struct bpf_insn *insns;

	if (prog->obj->loaded)
		return -EBUSY;

	insns = libbpf_reallocarray(prog->insns, new_insn_cnt, sizeof(*insns));
	/* NULL is a valid return from reallocarray if the new count is zero */
	if (!insns && new_insn_cnt) {
		pr_warn("prog '%s': failed to realloc prog code\n", prog->name);
		return -ENOMEM;
	}
	memcpy(insns, new_insns, new_insn_cnt * sizeof(*insns));

	prog->insns = insns;
	prog->insns_cnt = new_insn_cnt;
	return 0;
}

int bpf_program__fd(const struct bpf_program *prog)
{
	if (!prog)
		return libbpf_err(-EINVAL);

	if (prog->fd < 0)
		return libbpf_err(-ENOENT);

	return prog->fd;
}

__alias(bpf_program__type)
enum bpf_prog_type bpf_program__get_type(const struct bpf_program *prog);

enum bpf_prog_type bpf_program__type(const struct bpf_program *prog)
{
	return prog->type;
}

static size_t custom_sec_def_cnt;
static struct bpf_sec_def *custom_sec_defs;
static struct bpf_sec_def custom_fallback_def;
static bool has_custom_fallback_def;
static int last_custom_sec_def_handler_id;

int bpf_program__set_type(struct bpf_program *prog, enum bpf_prog_type type)
{
	if (prog->obj->loaded)
		return libbpf_err(-EBUSY);

	/* if type is not changed, do nothing */
	if (prog->type == type)
		return 0;

	prog->type = type;

	/* If a program type was changed, we need to reset associated SEC()
	 * handler, as it will be invalid now. The only exception is a generic
	 * fallback handler, which by definition is program type-agnostic and
	 * is a catch-all custom handler, optionally set by the application,
	 * so should be able to handle any type of BPF program.
	 */
	if (prog->sec_def != &custom_fallback_def)
		prog->sec_def = NULL;
	return 0;
}

__alias(bpf_program__expected_attach_type)
enum bpf_attach_type bpf_program__get_expected_attach_type(const struct bpf_program *prog);

enum bpf_attach_type bpf_program__expected_attach_type(const struct bpf_program *prog)
{
	return prog->expected_attach_type;
}

int bpf_program__set_expected_attach_type(struct bpf_program *prog,
					   enum bpf_attach_type type)
{
	if (prog->obj->loaded)
		return libbpf_err(-EBUSY);

	prog->expected_attach_type = type;
	return 0;
}

__u32 bpf_program__flags(const struct bpf_program *prog)
{
	return prog->prog_flags;
}

int bpf_program__set_flags(struct bpf_program *prog, __u32 flags)
{
	if (prog->obj->loaded)
		return libbpf_err(-EBUSY);

	prog->prog_flags = flags;
	return 0;
}

__u32 bpf_program__log_level(const struct bpf_program *prog)
{
	return prog->log_level;
}

int bpf_program__set_log_level(struct bpf_program *prog, __u32 log_level)
{
	if (prog->obj->loaded)
		return libbpf_err(-EBUSY);

	prog->log_level = log_level;
	return 0;
}

const char *bpf_program__log_buf(const struct bpf_program *prog, size_t *log_size)
{
	*log_size = prog->log_size;
	return prog->log_buf;
}

int bpf_program__set_log_buf(struct bpf_program *prog, char *log_buf, size_t log_size)
{
	if (log_size && !log_buf)
		return -EINVAL;
	if (prog->log_size > UINT_MAX)
		return -EINVAL;
	if (prog->obj->loaded)
		return -EBUSY;

	prog->log_buf = log_buf;
	prog->log_size = log_size;
	return 0;
}

#define SEC_DEF(sec_pfx, ptype, atype, flags, ...) {			    \
	.sec = (char *)sec_pfx,						    \
	.prog_type = BPF_PROG_TYPE_##ptype,				    \
	.expected_attach_type = atype,					    \
	.cookie = (long)(flags),					    \
	.prog_prepare_load_fn = libbpf_prepare_prog_load,		    \
	__VA_ARGS__							    \
}

static int attach_kprobe(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_uprobe(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_ksyscall(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_usdt(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_tp(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_raw_tp(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_trace(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_kprobe_multi(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_kprobe_session(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_uprobe_multi(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_lsm(const struct bpf_program *prog, long cookie, struct bpf_link **link);
static int attach_iter(const struct bpf_program *prog, long cookie, struct bpf_link **link);

static const struct bpf_sec_def section_defs[] = {
	SEC_DEF("socket",		SOCKET_FILTER, 0, SEC_NONE),
	SEC_DEF("sk_reuseport/migrate",	SK_REUSEPORT, BPF_SK_REUSEPORT_SELECT_OR_MIGRATE, SEC_ATTACHABLE),
	SEC_DEF("sk_reuseport",		SK_REUSEPORT, BPF_SK_REUSEPORT_SELECT, SEC_ATTACHABLE),
	SEC_DEF("kprobe+",		KPROBE,	0, SEC_NONE, attach_kprobe),
	SEC_DEF("uprobe+",		KPROBE,	0, SEC_NONE, attach_uprobe),
	SEC_DEF("uprobe.s+",		KPROBE,	0, SEC_SLEEPABLE, attach_uprobe),
	SEC_DEF("kretprobe+",		KPROBE, 0, SEC_NONE, attach_kprobe),
	SEC_DEF("uretprobe+",		KPROBE, 0, SEC_NONE, attach_uprobe),
	SEC_DEF("uretprobe.s+",		KPROBE, 0, SEC_SLEEPABLE, attach_uprobe),
	SEC_DEF("kprobe.multi+",	KPROBE,	BPF_TRACE_KPROBE_MULTI, SEC_NONE, attach_kprobe_multi),
	SEC_DEF("kretprobe.multi+",	KPROBE,	BPF_TRACE_KPROBE_MULTI, SEC_NONE, attach_kprobe_multi),
	SEC_DEF("kprobe.session+",	KPROBE,	BPF_TRACE_KPROBE_SESSION, SEC_NONE, attach_kprobe_session),
	SEC_DEF("uprobe.multi+",	KPROBE,	BPF_TRACE_UPROBE_MULTI, SEC_NONE, attach_uprobe_multi),
	SEC_DEF("uretprobe.multi+",	KPROBE,	BPF_TRACE_UPROBE_MULTI, SEC_NONE, attach_uprobe_multi),
	SEC_DEF("uprobe.multi.s+",	KPROBE,	BPF_TRACE_UPROBE_MULTI, SEC_SLEEPABLE, attach_uprobe_multi),
	SEC_DEF("uretprobe.multi.s+",	KPROBE,	BPF_TRACE_UPROBE_MULTI, SEC_SLEEPABLE, attach_uprobe_multi),
	SEC_DEF("ksyscall+",		KPROBE,	0, SEC_NONE, attach_ksyscall),
	SEC_DEF("kretsyscall+",		KPROBE, 0, SEC_NONE, attach_ksyscall),
	SEC_DEF("usdt+",		KPROBE,	0, SEC_USDT, attach_usdt),
	SEC_DEF("usdt.s+",		KPROBE,	0, SEC_USDT | SEC_SLEEPABLE, attach_usdt),
	SEC_DEF("tc/ingress",		SCHED_CLS, BPF_TCX_INGRESS, SEC_NONE), /* alias for tcx */
	SEC_DEF("tc/egress",		SCHED_CLS, BPF_TCX_EGRESS, SEC_NONE),  /* alias for tcx */
	SEC_DEF("tcx/ingress",		SCHED_CLS, BPF_TCX_INGRESS, SEC_NONE),
	SEC_DEF("tcx/egress",		SCHED_CLS, BPF_TCX_EGRESS, SEC_NONE),
	SEC_DEF("tc",			SCHED_CLS, 0, SEC_NONE), /* deprecated / legacy, use tcx */
	SEC_DEF("classifier",		SCHED_CLS, 0, SEC_NONE), /* deprecated / legacy, use tcx */
	SEC_DEF("action",		SCHED_ACT, 0, SEC_NONE), /* deprecated / legacy, use tcx */
	SEC_DEF("netkit/primary",	SCHED_CLS, BPF_NETKIT_PRIMARY, SEC_NONE),
	SEC_DEF("netkit/peer",		SCHED_CLS, BPF_NETKIT_PEER, SEC_NONE),
	SEC_DEF("tracepoint+",		TRACEPOINT, 0, SEC_NONE, attach_tp),
	SEC_DEF("tp+",			TRACEPOINT, 0, SEC_NONE, attach_tp),
	SEC_DEF("raw_tracepoint+",	RAW_TRACEPOINT, 0, SEC_NONE, attach_raw_tp),
	SEC_DEF("raw_tp+",		RAW_TRACEPOINT, 0, SEC_NONE, attach_raw_tp),
	SEC_DEF("raw_tracepoint.w+",	RAW_TRACEPOINT_WRITABLE, 0, SEC_NONE, attach_raw_tp),
	SEC_DEF("raw_tp.w+",		RAW_TRACEPOINT_WRITABLE, 0, SEC_NONE, attach_raw_tp),
	SEC_DEF("tp_btf+",		TRACING, BPF_TRACE_RAW_TP, SEC_ATTACH_BTF, attach_trace),
	SEC_DEF("fentry+",		TRACING, BPF_TRACE_FENTRY, SEC_ATTACH_BTF, attach_trace),
	SEC_DEF("fmod_ret+",		TRACING, BPF_MODIFY_RETURN, SEC_ATTACH_BTF, attach_trace),
	SEC_DEF("fexit+",		TRACING, BPF_TRACE_FEXIT, SEC_ATTACH_BTF, attach_trace),
	SEC_DEF("fentry.s+",		TRACING, BPF_TRACE_FENTRY, SEC_ATTACH_BTF | SEC_SLEEPABLE, attach_trace),
	SEC_DEF("fmod_ret.s+",		TRACING, BPF_MODIFY_RETURN, SEC_ATTACH_BTF | SEC_SLEEPABLE, attach_trace),
	SEC_DEF("fexit.s+",		TRACING, BPF_TRACE_FEXIT, SEC_ATTACH_BTF | SEC_SLEEPABLE, attach_trace),
	SEC_DEF("freplace+",		EXT, 0, SEC_ATTACH_BTF, attach_trace),
	SEC_DEF("lsm+",			LSM, BPF_LSM_MAC, SEC_ATTACH_BTF, attach_lsm),
	SEC_DEF("lsm.s+",		LSM, BPF_LSM_MAC, SEC_ATTACH_BTF | SEC_SLEEPABLE, attach_lsm),
	SEC_DEF("lsm_cgroup+",		LSM, BPF_LSM_CGROUP, SEC_ATTACH_BTF),
	SEC_DEF("iter+",		TRACING, BPF_TRACE_ITER, SEC_ATTACH_BTF, attach_iter),
	SEC_DEF("iter.s+",		TRACING, BPF_TRACE_ITER, SEC_ATTACH_BTF | SEC_SLEEPABLE, attach_iter),
	SEC_DEF("syscall",		SYSCALL, 0, SEC_SLEEPABLE),
	SEC_DEF("xdp.frags/devmap",	XDP, BPF_XDP_DEVMAP, SEC_XDP_FRAGS),
	SEC_DEF("xdp/devmap",		XDP, BPF_XDP_DEVMAP, SEC_ATTACHABLE),
	SEC_DEF("xdp.frags/cpumap",	XDP, BPF_XDP_CPUMAP, SEC_XDP_FRAGS),
	SEC_DEF("xdp/cpumap",		XDP, BPF_XDP_CPUMAP, SEC_ATTACHABLE),
	SEC_DEF("xdp.frags",		XDP, BPF_XDP, SEC_XDP_FRAGS),
	SEC_DEF("xdp",			XDP, BPF_XDP, SEC_ATTACHABLE_OPT),
	SEC_DEF("perf_event",		PERF_EVENT, 0, SEC_NONE),
	SEC_DEF("lwt_in",		LWT_IN, 0, SEC_NONE),
	SEC_DEF("lwt_out",		LWT_OUT, 0, SEC_NONE),
	SEC_DEF("lwt_xmit",		LWT_XMIT, 0, SEC_NONE),
	SEC_DEF("lwt_seg6local",	LWT_SEG6LOCAL, 0, SEC_NONE),
	SEC_DEF("sockops",		SOCK_OPS, BPF_CGROUP_SOCK_OPS, SEC_ATTACHABLE_OPT),
	SEC_DEF("sk_skb/stream_parser",	SK_SKB, BPF_SK_SKB_STREAM_PARSER, SEC_ATTACHABLE_OPT),
	SEC_DEF("sk_skb/stream_verdict",SK_SKB, BPF_SK_SKB_STREAM_VERDICT, SEC_ATTACHABLE_OPT),
	SEC_DEF("sk_skb/verdict",	SK_SKB, BPF_SK_SKB_VERDICT, SEC_ATTACHABLE_OPT),
	SEC_DEF("sk_skb",		SK_SKB, 0, SEC_NONE),
	SEC_DEF("sk_msg",		SK_MSG, BPF_SK_MSG_VERDICT, SEC_ATTACHABLE_OPT),
	SEC_DEF("lirc_mode2",		LIRC_MODE2, BPF_LIRC_MODE2, SEC_ATTACHABLE_OPT),
	SEC_DEF("flow_dissector",	FLOW_DISSECTOR, BPF_FLOW_DISSECTOR, SEC_ATTACHABLE_OPT),
	SEC_DEF("cgroup_skb/ingress",	CGROUP_SKB, BPF_CGROUP_INET_INGRESS, SEC_ATTACHABLE_OPT),
	SEC_DEF("cgroup_skb/egress",	CGROUP_SKB, BPF_CGROUP_INET_EGRESS, SEC_ATTACHABLE_OPT),
	SEC_DEF("cgroup/skb",		CGROUP_SKB, 0, SEC_NONE),
	SEC_DEF("cgroup/sock_create",	CGROUP_SOCK, BPF_CGROUP_INET_SOCK_CREATE, SEC_ATTACHABLE),
	SEC_DEF("cgroup/sock_release",	CGROUP_SOCK, BPF_CGROUP_INET_SOCK_RELEASE, SEC_ATTACHABLE),
	SEC_DEF("cgroup/sock",		CGROUP_SOCK, BPF_CGROUP_INET_SOCK_CREATE, SEC_ATTACHABLE_OPT),
	SEC_DEF("cgroup/post_bind4",	CGROUP_SOCK, BPF_CGROUP_INET4_POST_BIND, SEC_ATTACHABLE),
	SEC_DEF("cgroup/post_bind6",	CGROUP_SOCK, BPF_CGROUP_INET6_POST_BIND, SEC_ATTACHABLE),
	SEC_DEF("cgroup/bind4",		CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_BIND, SEC_ATTACHABLE),
	SEC_DEF("cgroup/bind6",		CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_BIND, SEC_ATTACHABLE),
	SEC_DEF("cgroup/connect4",	CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_CONNECT, SEC_ATTACHABLE),
	SEC_DEF("cgroup/connect6",	CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_CONNECT, SEC_ATTACHABLE),
	SEC_DEF("cgroup/connect_unix",	CGROUP_SOCK_ADDR, BPF_CGROUP_UNIX_CONNECT, SEC_ATTACHABLE),
	SEC_DEF("cgroup/sendmsg4",	CGROUP_SOCK_ADDR, BPF_CGROUP_UDP4_SENDMSG, SEC_ATTACHABLE),
	SEC_DEF("cgroup/sendmsg6",	CGROUP_SOCK_ADDR, BPF_CGROUP_UDP6_SENDMSG, SEC_ATTACHABLE),
	SEC_DEF("cgroup/sendmsg_unix",	CGROUP_SOCK_ADDR, BPF_CGROUP_UNIX_SENDMSG, SEC_ATTACHABLE),
	SEC_DEF("cgroup/recvmsg4",	CGROUP_SOCK_ADDR, BPF_CGROUP_UDP4_RECVMSG, SEC_ATTACHABLE),
	SEC_DEF("cgroup/recvmsg6",	CGROUP_SOCK_ADDR, BPF_CGROUP_UDP6_RECVMSG, SEC_ATTACHABLE),
	SEC_DEF("cgroup/recvmsg_unix",	CGROUP_SOCK_ADDR, BPF_CGROUP_UNIX_RECVMSG, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getpeername4",	CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_GETPEERNAME, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getpeername6",	CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_GETPEERNAME, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getpeername_unix", CGROUP_SOCK_ADDR, BPF_CGROUP_UNIX_GETPEERNAME, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getsockname4",	CGROUP_SOCK_ADDR, BPF_CGROUP_INET4_GETSOCKNAME, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getsockname6",	CGROUP_SOCK_ADDR, BPF_CGROUP_INET6_GETSOCKNAME, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getsockname_unix", CGROUP_SOCK_ADDR, BPF_CGROUP_UNIX_GETSOCKNAME, SEC_ATTACHABLE),
	SEC_DEF("cgroup/sysctl",	CGROUP_SYSCTL, BPF_CGROUP_SYSCTL, SEC_ATTACHABLE),
	SEC_DEF("cgroup/getsockopt",	CGROUP_SOCKOPT, BPF_CGROUP_GETSOCKOPT, SEC_ATTACHABLE),
	SEC_DEF("cgroup/setsockopt",	CGROUP_SOCKOPT, BPF_CGROUP_SETSOCKOPT, SEC_ATTACHABLE),
	SEC_DEF("cgroup/dev",		CGROUP_DEVICE, BPF_CGROUP_DEVICE, SEC_ATTACHABLE_OPT),
	SEC_DEF("struct_ops+",		STRUCT_OPS, 0, SEC_NONE),
	SEC_DEF("struct_ops.s+",	STRUCT_OPS, 0, SEC_SLEEPABLE),
	SEC_DEF("sk_lookup",		SK_LOOKUP, BPF_SK_LOOKUP, SEC_ATTACHABLE),
	SEC_DEF("netfilter",		NETFILTER, BPF_NETFILTER, SEC_NONE),
};

int libbpf_register_prog_handler(const char *sec,
				 enum bpf_prog_type prog_type,
				 enum bpf_attach_type exp_attach_type,
				 const struct libbpf_prog_handler_opts *opts)
{
	struct bpf_sec_def *sec_def;

	if (!OPTS_VALID(opts, libbpf_prog_handler_opts))
		return libbpf_err(-EINVAL);

	if (last_custom_sec_def_handler_id == INT_MAX) /* prevent overflow */
		return libbpf_err(-E2BIG);

	if (sec) {
		sec_def = libbpf_reallocarray(custom_sec_defs, custom_sec_def_cnt + 1,
					      sizeof(*sec_def));
		if (!sec_def)
			return libbpf_err(-ENOMEM);

		custom_sec_defs = sec_def;
		sec_def = &custom_sec_defs[custom_sec_def_cnt];
	} else {
		if (has_custom_fallback_def)
			return libbpf_err(-EBUSY);

		sec_def = &custom_fallback_def;
	}

	sec_def->sec = sec ? strdup(sec) : NULL;
	if (sec && !sec_def->sec)
		return libbpf_err(-ENOMEM);

	sec_def->prog_type = prog_type;
	sec_def->expected_attach_type = exp_attach_type;
	sec_def->cookie = OPTS_GET(opts, cookie, 0);

	sec_def->prog_setup_fn = OPTS_GET(opts, prog_setup_fn, NULL);
	sec_def->prog_prepare_load_fn = OPTS_GET(opts, prog_prepare_load_fn, NULL);
	sec_def->prog_attach_fn = OPTS_GET(opts, prog_attach_fn, NULL);

	sec_def->handler_id = ++last_custom_sec_def_handler_id;

	if (sec)
		custom_sec_def_cnt++;
	else
		has_custom_fallback_def = true;

	return sec_def->handler_id;
}

int libbpf_unregister_prog_handler(int handler_id)
{
	struct bpf_sec_def *sec_defs;
	int i;

	if (handler_id <= 0)
		return libbpf_err(-EINVAL);

	if (has_custom_fallback_def && custom_fallback_def.handler_id == handler_id) {
		memset(&custom_fallback_def, 0, sizeof(custom_fallback_def));
		has_custom_fallback_def = false;
		return 0;
	}

	for (i = 0; i < custom_sec_def_cnt; i++) {
		if (custom_sec_defs[i].handler_id == handler_id)
			break;
	}

	if (i == custom_sec_def_cnt)
		return libbpf_err(-ENOENT);

	free(custom_sec_defs[i].sec);
	for (i = i + 1; i < custom_sec_def_cnt; i++)
		custom_sec_defs[i - 1] = custom_sec_defs[i];
	custom_sec_def_cnt--;

	/* try to shrink the array, but it's ok if we couldn't */
	sec_defs = libbpf_reallocarray(custom_sec_defs, custom_sec_def_cnt, sizeof(*sec_defs));
	/* if new count is zero, reallocarray can return a valid NULL result;
	 * in this case the previous pointer will be freed, so we *have to*
	 * reassign old pointer to the new value (even if it's NULL)
	 */
	if (sec_defs || custom_sec_def_cnt == 0)
		custom_sec_defs = sec_defs;

	return 0;
}

static bool sec_def_matches(const struct bpf_sec_def *sec_def, const char *sec_name)
{
	size_t len = strlen(sec_def->sec);

	/* "type/" always has to have proper SEC("type/extras") form */
	if (sec_def->sec[len - 1] == '/') {
		if (str_has_pfx(sec_name, sec_def->sec))
			return true;
		return false;
	}

	/* "type+" means it can be either exact SEC("type") or
	 * well-formed SEC("type/extras") with proper '/' separator
	 */
	if (sec_def->sec[len - 1] == '+') {
		len--;
		/* not even a prefix */
		if (strncmp(sec_name, sec_def->sec, len) != 0)
			return false;
		/* exact match or has '/' separator */
		if (sec_name[len] == '\0' || sec_name[len] == '/')
			return true;
		return false;
	}

	return strcmp(sec_name, sec_def->sec) == 0;
}

static const struct bpf_sec_def *find_sec_def(const char *sec_name)
{
	const struct bpf_sec_def *sec_def;
	int i, n;

	n = custom_sec_def_cnt;
	for (i = 0; i < n; i++) {
		sec_def = &custom_sec_defs[i];
		if (sec_def_matches(sec_def, sec_name))
			return sec_def;
	}

	n = ARRAY_SIZE(section_defs);
	for (i = 0; i < n; i++) {
		sec_def = &section_defs[i];
		if (sec_def_matches(sec_def, sec_name))
			return sec_def;
	}

	if (has_custom_fallback_def)
		return &custom_fallback_def;

	return NULL;
}

#define MAX_TYPE_NAME_SIZE 32

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
		const struct bpf_sec_def *sec_def = &section_defs[i];

		if (attach_type) {
			if (sec_def->prog_prepare_load_fn != libbpf_prepare_prog_load)
				continue;

			if (!(sec_def->cookie & SEC_ATTACHABLE))
				continue;
		}

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

const char *libbpf_bpf_attach_type_str(enum bpf_attach_type t)
{
	if (t < 0 || t >= ARRAY_SIZE(attach_type_name))
		return NULL;

	return attach_type_name[t];
}

const char *libbpf_bpf_link_type_str(enum bpf_link_type t)
{
	if (t < 0 || t >= ARRAY_SIZE(link_type_name))
		return NULL;

	return link_type_name[t];
}

const char *libbpf_bpf_map_type_str(enum bpf_map_type t)
{
	if (t < 0 || t >= ARRAY_SIZE(map_type_name))
		return NULL;

	return map_type_name[t];
}

const char *libbpf_bpf_prog_type_str(enum bpf_prog_type t)
{
	if (t < 0 || t >= ARRAY_SIZE(prog_type_name))
		return NULL;

	return prog_type_name[t];
}

static struct bpf_map *find_struct_ops_map_by_offset(struct bpf_object *obj,
						     int sec_idx,
						     size_t offset)
{
	struct bpf_map *map;
	size_t i;

	for (i = 0; i < obj->nr_maps; i++) {
		map = &obj->maps[i];
		if (!bpf_map__is_struct_ops(map))
			continue;
		if (map->sec_idx == sec_idx &&
		    map->sec_offset <= offset &&
		    offset - map->sec_offset < map->def.value_size)
			return map;
	}

	return NULL;
}

/* Collect the reloc from ELF, populate the st_ops->progs[], and update
 * st_ops->data for shadow type.
 */
static int bpf_object__collect_st_ops_relos(struct bpf_object *obj,
					    Elf64_Shdr *shdr, Elf_Data *data)
{
	const struct btf_member *member;
	struct bpf_struct_ops *st_ops;
	struct bpf_program *prog;
	unsigned int shdr_idx;
	const struct btf *btf;
	struct bpf_map *map;
	unsigned int moff, insn_idx;
	const char *name;
	__u32 member_idx;
	Elf64_Sym *sym;
	Elf64_Rel *rel;
	int i, nrels;

	btf = obj->btf;
	nrels = shdr->sh_size / shdr->sh_entsize;
	for (i = 0; i < nrels; i++) {
		rel = elf_rel_by_idx(data, i);
		if (!rel) {
			pr_warn("struct_ops reloc: failed to get %d reloc\n", i);
			return -LIBBPF_ERRNO__FORMAT;
		}

		sym = elf_sym_by_idx(obj, ELF64_R_SYM(rel->r_info));
		if (!sym) {
			pr_warn("struct_ops reloc: symbol %zx not found\n",
				(size_t)ELF64_R_SYM(rel->r_info));
			return -LIBBPF_ERRNO__FORMAT;
		}

		name = elf_sym_str(obj, sym->st_name) ?: "<?>";
		map = find_struct_ops_map_by_offset(obj, shdr->sh_info, rel->r_offset);
		if (!map) {
			pr_warn("struct_ops reloc: cannot find map at rel->r_offset %zu\n",
				(size_t)rel->r_offset);
			return -EINVAL;
		}

		moff = rel->r_offset - map->sec_offset;
		shdr_idx = sym->st_shndx;
		st_ops = map->st_ops;
		pr_debug("struct_ops reloc %s: for %lld value %lld shdr_idx %u rel->r_offset %zu map->sec_offset %zu name %d (\'%s\')\n",
			 map->name,
			 (long long)(rel->r_info >> 32),
			 (long long)sym->st_value,
			 shdr_idx, (size_t)rel->r_offset,
			 map->sec_offset, sym->st_name, name);

		if (shdr_idx >= SHN_LORESERVE) {
			pr_warn("struct_ops reloc %s: rel->r_offset %zu shdr_idx %u unsupported non-static function\n",
				map->name, (size_t)rel->r_offset, shdr_idx);
			return -LIBBPF_ERRNO__RELOC;
		}
		if (sym->st_value % BPF_INSN_SZ) {
			pr_warn("struct_ops reloc %s: invalid target program offset %llu\n",
				map->name, (unsigned long long)sym->st_value);
			return -LIBBPF_ERRNO__FORMAT;
		}
		insn_idx = sym->st_value / BPF_INSN_SZ;

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

		st_ops->progs[member_idx] = prog;

		/* st_ops->data will be exposed to users, being returned by
		 * bpf_map__initial_value() as a pointer to the shadow
		 * type. All function pointers in the original struct type
		 * should be converted to a pointer to struct bpf_program
		 * in the shadow type.
		 */
		*((struct bpf_program **)(st_ops->data + moff)) = prog;
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
	case BPF_LSM_CGROUP:
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
	struct bpf_prog_info info;
	__u32 info_len = sizeof(info);
	struct btf *btf;
	int err;

	memset(&info, 0, info_len);
	err = bpf_prog_get_info_by_fd(attach_prog_fd, &info, &info_len);
	if (err) {
		pr_warn("failed bpf_prog_get_info_by_fd for FD %d: %d\n",
			attach_prog_fd, err);
		return err;
	}

	err = -EINVAL;
	if (!info.btf_id) {
		pr_warn("The target program doesn't have BTF\n");
		goto out;
	}
	btf = btf__load_from_kernel_by_id(info.btf_id);
	err = libbpf_get_error(btf);
	if (err) {
		pr_warn("Failed to get BTF %d of the program: %d\n", info.btf_id, err);
		goto out;
	}
	err = btf__find_by_name_kind(btf, name, BTF_KIND_FUNC);
	btf__free(btf);
	if (err <= 0) {
		pr_warn("%s is not found in prog's BTF\n", name);
		goto out;
	}
out:
	return err;
}

static int find_kernel_btf_id(struct bpf_object *obj, const char *attach_name,
			      enum bpf_attach_type attach_type,
			      int *btf_obj_fd, int *btf_type_id)
{
	int ret, i, mod_len;
	const char *fn_name, *mod_name = NULL;

	fn_name = strchr(attach_name, ':');
	if (fn_name) {
		mod_name = attach_name;
		mod_len = fn_name - mod_name;
		fn_name++;
	}

	if (!mod_name || strncmp(mod_name, "vmlinux", mod_len) == 0) {
		ret = find_attach_btf_id(obj->btf_vmlinux,
					 mod_name ? fn_name : attach_name,
					 attach_type);
		if (ret > 0) {
			*btf_obj_fd = 0; /* vmlinux BTF */
			*btf_type_id = ret;
			return 0;
		}
		if (ret != -ENOENT)
			return ret;
	}

	ret = load_module_btfs(obj);
	if (ret)
		return ret;

	for (i = 0; i < obj->btf_module_cnt; i++) {
		const struct module_btf *mod = &obj->btf_modules[i];

		if (mod_name && strncmp(mod->name, mod_name, mod_len) != 0)
			continue;

		ret = find_attach_btf_id(mod->btf,
					 mod_name ? fn_name : attach_name,
					 attach_type);
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

static int libbpf_find_attach_btf_id(struct bpf_program *prog, const char *attach_name,
				     int *btf_obj_fd, int *btf_type_id)
{
	enum bpf_attach_type attach_type = prog->expected_attach_type;
	__u32 attach_prog_fd = prog->attach_prog_fd;
	int err = 0;

	/* BPF program's BTF ID */
	if (prog->type == BPF_PROG_TYPE_EXT || attach_prog_fd) {
		if (!attach_prog_fd) {
			pr_warn("prog '%s': attach program FD is not set\n", prog->name);
			return -EINVAL;
		}
		err = libbpf_find_prog_btf_id(attach_name, attach_prog_fd);
		if (err < 0) {
			pr_warn("prog '%s': failed to find BPF program (FD %d) BTF ID for '%s': %d\n",
				 prog->name, attach_prog_fd, attach_name, err);
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
		err = find_kernel_btf_id(prog->obj, attach_name,
					 attach_type, btf_obj_fd,
					 btf_type_id);
	}
	if (err) {
		pr_warn("prog '%s': failed to find kernel BTF type ID of '%s': %d\n",
			prog->name, attach_name, err);
		return err;
	}
	return 0;
}

int libbpf_attach_type_by_name(const char *name,
			       enum bpf_attach_type *attach_type)
{
	char *type_names;
	const struct bpf_sec_def *sec_def;

	if (!name)
		return libbpf_err(-EINVAL);

	sec_def = find_sec_def(name);
	if (!sec_def) {
		pr_debug("failed to guess attach type based on ELF section name '%s'\n", name);
		type_names = libbpf_get_type_names(true);
		if (type_names != NULL) {
			pr_debug("attachable section(type) names are:%s\n", type_names);
			free(type_names);
		}

		return libbpf_err(-EINVAL);
	}

	if (sec_def->prog_prepare_load_fn != libbpf_prepare_prog_load)
		return libbpf_err(-EINVAL);
	if (!(sec_def->cookie & SEC_ATTACHABLE))
		return libbpf_err(-EINVAL);

	*attach_type = sec_def->expected_attach_type;
	return 0;
}

int bpf_map__fd(const struct bpf_map *map)
{
	if (!map)
		return libbpf_err(-EINVAL);
	if (!map_is_created(map))
		return -1;
	return map->fd;
}

static bool map_uses_real_name(const struct bpf_map *map)
{
	/* Since libbpf started to support custom .data.* and .rodata.* maps,
	 * their user-visible name differs from kernel-visible name. Users see
	 * such map's corresponding ELF section name as a map name.
	 * This check distinguishes .data/.rodata from .data.* and .rodata.*
	 * maps to know which name has to be returned to the user.
	 */
	if (map->libbpf_type == LIBBPF_MAP_DATA && strcmp(map->real_name, DATA_SEC) != 0)
		return true;
	if (map->libbpf_type == LIBBPF_MAP_RODATA && strcmp(map->real_name, RODATA_SEC) != 0)
		return true;
	return false;
}

const char *bpf_map__name(const struct bpf_map *map)
{
	if (!map)
		return NULL;

	if (map_uses_real_name(map))
		return map->real_name;

	return map->name;
}

enum bpf_map_type bpf_map__type(const struct bpf_map *map)
{
	return map->def.type;
}

int bpf_map__set_type(struct bpf_map *map, enum bpf_map_type type)
{
	if (map_is_created(map))
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
	if (map_is_created(map))
		return libbpf_err(-EBUSY);
	map->def.map_flags = flags;
	return 0;
}

__u64 bpf_map__map_extra(const struct bpf_map *map)
{
	return map->map_extra;
}

int bpf_map__set_map_extra(struct bpf_map *map, __u64 map_extra)
{
	if (map_is_created(map))
		return libbpf_err(-EBUSY);
	map->map_extra = map_extra;
	return 0;
}

__u32 bpf_map__numa_node(const struct bpf_map *map)
{
	return map->numa_node;
}

int bpf_map__set_numa_node(struct bpf_map *map, __u32 numa_node)
{
	if (map_is_created(map))
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
	if (map_is_created(map))
		return libbpf_err(-EBUSY);
	map->def.key_size = size;
	return 0;
}

__u32 bpf_map__value_size(const struct bpf_map *map)
{
	return map->def.value_size;
}

static int map_btf_datasec_resize(struct bpf_map *map, __u32 size)
{
	struct btf *btf;
	struct btf_type *datasec_type, *var_type;
	struct btf_var_secinfo *var;
	const struct btf_type *array_type;
	const struct btf_array *array;
	int vlen, element_sz, new_array_id;
	__u32 nr_elements;

	/* check btf existence */
	btf = bpf_object__btf(map->obj);
	if (!btf)
		return -ENOENT;

	/* verify map is datasec */
	datasec_type = btf_type_by_id(btf, bpf_map__btf_value_type_id(map));
	if (!btf_is_datasec(datasec_type)) {
		pr_warn("map '%s': cannot be resized, map value type is not a datasec\n",
			bpf_map__name(map));
		return -EINVAL;
	}

	/* verify datasec has at least one var */
	vlen = btf_vlen(datasec_type);
	if (vlen == 0) {
		pr_warn("map '%s': cannot be resized, map value datasec is empty\n",
			bpf_map__name(map));
		return -EINVAL;
	}

	/* verify last var in the datasec is an array */
	var = &btf_var_secinfos(datasec_type)[vlen - 1];
	var_type = btf_type_by_id(btf, var->type);
	array_type = skip_mods_and_typedefs(btf, var_type->type, NULL);
	if (!btf_is_array(array_type)) {
		pr_warn("map '%s': cannot be resized, last var must be an array\n",
			bpf_map__name(map));
		return -EINVAL;
	}

	/* verify request size aligns with array */
	array = btf_array(array_type);
	element_sz = btf__resolve_size(btf, array->type);
	if (element_sz <= 0 || (size - var->offset) % element_sz != 0) {
		pr_warn("map '%s': cannot be resized, element size (%d) doesn't align with new total size (%u)\n",
			bpf_map__name(map), element_sz, size);
		return -EINVAL;
	}

	/* create a new array based on the existing array, but with new length */
	nr_elements = (size - var->offset) / element_sz;
	new_array_id = btf__add_array(btf, array->index_type, array->type, nr_elements);
	if (new_array_id < 0)
		return new_array_id;

	/* adding a new btf type invalidates existing pointers to btf objects,
	 * so refresh pointers before proceeding
	 */
	datasec_type = btf_type_by_id(btf, map->btf_value_type_id);
	var = &btf_var_secinfos(datasec_type)[vlen - 1];
	var_type = btf_type_by_id(btf, var->type);

	/* finally update btf info */
	datasec_type->size = size;
	var->size = size - var->offset;
	var_type->type = new_array_id;

	return 0;
}

int bpf_map__set_value_size(struct bpf_map *map, __u32 size)
{
	if (map->obj->loaded || map->reused)
		return libbpf_err(-EBUSY);

	if (map->mmaped) {
		size_t mmap_old_sz, mmap_new_sz;
		int err;

		if (map->def.type != BPF_MAP_TYPE_ARRAY)
			return -EOPNOTSUPP;

		mmap_old_sz = bpf_map_mmap_sz(map);
		mmap_new_sz = array_map_mmap_sz(size, map->def.max_entries);
		err = bpf_map_mmap_resize(map, mmap_old_sz, mmap_new_sz);
		if (err) {
			pr_warn("map '%s': failed to resize memory-mapped region: %d\n",
				bpf_map__name(map), err);
			return err;
		}
		err = map_btf_datasec_resize(map, size);
		if (err && err != -ENOENT) {
			pr_warn("map '%s': failed to adjust resized BTF, clearing BTF key/value info: %d\n",
				bpf_map__name(map), err);
			map->btf_value_type_id = 0;
			map->btf_key_type_id = 0;
		}
	}

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

int bpf_map__set_initial_value(struct bpf_map *map,
			       const void *data, size_t size)
{
	size_t actual_sz;

	if (map->obj->loaded || map->reused)
		return libbpf_err(-EBUSY);

	if (!map->mmaped || map->libbpf_type == LIBBPF_MAP_KCONFIG)
		return libbpf_err(-EINVAL);

	if (map->def.type == BPF_MAP_TYPE_ARENA)
		actual_sz = map->obj->arena_data_sz;
	else
		actual_sz = map->def.value_size;
	if (size != actual_sz)
		return libbpf_err(-EINVAL);

	memcpy(map->mmaped, data, size);
	return 0;
}

void *bpf_map__initial_value(const struct bpf_map *map, size_t *psize)
{
	if (bpf_map__is_struct_ops(map)) {
		if (psize)
			*psize = map->def.value_size;
		return map->st_ops->data;
	}

	if (!map->mmaped)
		return NULL;

	if (map->def.type == BPF_MAP_TYPE_ARENA)
		*psize = map->obj->arena_data_sz;
	else
		*psize = map->def.value_size;

	return map->mmaped;
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
	if (map_is_created(map))
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
	if (map->inner_map) {
		bpf_map__destroy(map->inner_map);
		zfree(&map->inner_map);
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
bpf_object__next_map(const struct bpf_object *obj, const struct bpf_map *prev)
{
	if (prev == NULL)
		return obj->maps;

	return __bpf_map__iter(prev, obj, 1);
}

struct bpf_map *
bpf_object__prev_map(const struct bpf_object *obj, const struct bpf_map *next)
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
		/* if it's a special internal map name (which always starts
		 * with dot) then check if that special name matches the
		 * real map name (ELF section name)
		 */
		if (name[0] == '.') {
			if (pos->real_name && strcmp(pos->real_name, name) == 0)
				return pos;
			continue;
		}
		/* otherwise map name has to be an exact match */
		if (map_uses_real_name(pos)) {
			if (strcmp(pos->real_name, name) == 0)
				return pos;
			continue;
		}
		if (strcmp(pos->name, name) == 0)
			return pos;
	}
	return errno = ENOENT, NULL;
}

int
bpf_object__find_map_fd_by_name(const struct bpf_object *obj, const char *name)
{
	return bpf_map__fd(bpf_object__find_map_by_name(obj, name));
}

static int validate_map_op(const struct bpf_map *map, size_t key_sz,
			   size_t value_sz, bool check_value_sz)
{
	if (!map_is_created(map)) /* map is not yet created */
		return -ENOENT;

	if (map->def.key_size != key_sz) {
		pr_warn("map '%s': unexpected key size %zu provided, expected %u\n",
			map->name, key_sz, map->def.key_size);
		return -EINVAL;
	}

	if (map->fd < 0) {
		pr_warn("map '%s': can't use BPF map without FD (was it created?)\n", map->name);
		return -EINVAL;
	}

	if (!check_value_sz)
		return 0;

	switch (map->def.type) {
	case BPF_MAP_TYPE_PERCPU_ARRAY:
	case BPF_MAP_TYPE_PERCPU_HASH:
	case BPF_MAP_TYPE_LRU_PERCPU_HASH:
	case BPF_MAP_TYPE_PERCPU_CGROUP_STORAGE: {
		int num_cpu = libbpf_num_possible_cpus();
		size_t elem_sz = roundup(map->def.value_size, 8);

		if (value_sz != num_cpu * elem_sz) {
			pr_warn("map '%s': unexpected value size %zu provided for per-CPU map, expected %d * %zu = %zd\n",
				map->name, value_sz, num_cpu, elem_sz, num_cpu * elem_sz);
			return -EINVAL;
		}
		break;
	}
	default:
		if (map->def.value_size != value_sz) {
			pr_warn("map '%s': unexpected value size %zu provided, expected %u\n",
				map->name, value_sz, map->def.value_size);
			return -EINVAL;
		}
		break;
	}
	return 0;
}

int bpf_map__lookup_elem(const struct bpf_map *map,
			 const void *key, size_t key_sz,
			 void *value, size_t value_sz, __u64 flags)
{
	int err;

	err = validate_map_op(map, key_sz, value_sz, true);
	if (err)
		return libbpf_err(err);

	return bpf_map_lookup_elem_flags(map->fd, key, value, flags);
}

int bpf_map__update_elem(const struct bpf_map *map,
			 const void *key, size_t key_sz,
			 const void *value, size_t value_sz, __u64 flags)
{
	int err;

	err = validate_map_op(map, key_sz, value_sz, true);
	if (err)
		return libbpf_err(err);

	return bpf_map_update_elem(map->fd, key, value, flags);
}

int bpf_map__delete_elem(const struct bpf_map *map,
			 const void *key, size_t key_sz, __u64 flags)
{
	int err;

	err = validate_map_op(map, key_sz, 0, false /* check_value_sz */);
	if (err)
		return libbpf_err(err);

	return bpf_map_delete_elem_flags(map->fd, key, flags);
}

int bpf_map__lookup_and_delete_elem(const struct bpf_map *map,
				    const void *key, size_t key_sz,
				    void *value, size_t value_sz, __u64 flags)
{
	int err;

	err = validate_map_op(map, key_sz, value_sz, true);
	if (err)
		return libbpf_err(err);

	return bpf_map_lookup_and_delete_elem_flags(map->fd, key, value, flags);
}

int bpf_map__get_next_key(const struct bpf_map *map,
			  const void *cur_key, void *next_key, size_t key_sz)
{
	int err;

	err = validate_map_op(map, key_sz, 0, false /* check_value_sz */);
	if (err)
		return libbpf_err(err);

	return bpf_map_get_next_key(map->fd, cur_key, next_key);
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

/* Replace link's underlying BPF program with the new one */
int bpf_link__update_program(struct bpf_link *link, struct bpf_program *prog)
{
	int ret;
	int prog_fd = bpf_program__fd(prog);

	if (prog_fd < 0) {
		pr_warn("prog '%s': can't use BPF program without FD (was it loaded?)\n",
			prog->name);
		return libbpf_err(-EINVAL);
	}

	ret = bpf_link_update(bpf_link__fd(link), prog_fd, NULL);
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

struct bpf_link_perf {
	struct bpf_link link;
	int perf_event_fd;
	/* legacy kprobe support: keep track of probe identifier and type */
	char *legacy_probe_name;
	bool legacy_is_kprobe;
	bool legacy_is_retprobe;
};

static int remove_kprobe_event_legacy(const char *probe_name, bool retprobe);
static int remove_uprobe_event_legacy(const char *probe_name, bool retprobe);

static int bpf_link_perf_detach(struct bpf_link *link)
{
	struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);
	int err = 0;

	if (ioctl(perf_link->perf_event_fd, PERF_EVENT_IOC_DISABLE, 0) < 0)
		err = -errno;

	if (perf_link->perf_event_fd != link->fd)
		close(perf_link->perf_event_fd);
	close(link->fd);

	/* legacy uprobe/kprobe needs to be removed after perf event fd closure */
	if (perf_link->legacy_probe_name) {
		if (perf_link->legacy_is_kprobe) {
			err = remove_kprobe_event_legacy(perf_link->legacy_probe_name,
							 perf_link->legacy_is_retprobe);
		} else {
			err = remove_uprobe_event_legacy(perf_link->legacy_probe_name,
							 perf_link->legacy_is_retprobe);
		}
	}

	return err;
}

static void bpf_link_perf_dealloc(struct bpf_link *link)
{
	struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);

	free(perf_link->legacy_probe_name);
	free(perf_link);
}

struct bpf_link *bpf_program__attach_perf_event_opts(const struct bpf_program *prog, int pfd,
						     const struct bpf_perf_event_opts *opts)
{
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link_perf *link;
	int prog_fd, link_fd = -1, err;
	bool force_ioctl_attach;

	if (!OPTS_VALID(opts, bpf_perf_event_opts))
		return libbpf_err_ptr(-EINVAL);

	if (pfd < 0) {
		pr_warn("prog '%s': invalid perf event FD %d\n",
			prog->name, pfd);
		return libbpf_err_ptr(-EINVAL);
	}
	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach BPF program without FD (was it loaded?)\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->link.detach = &bpf_link_perf_detach;
	link->link.dealloc = &bpf_link_perf_dealloc;
	link->perf_event_fd = pfd;

	force_ioctl_attach = OPTS_GET(opts, force_ioctl_attach, false);
	if (kernel_supports(prog->obj, FEAT_PERF_LINK) && !force_ioctl_attach) {
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

struct bpf_link *bpf_program__attach_perf_event(const struct bpf_program *prog, int pfd)
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

	f = fopen(file, "re");
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

#define PERF_UPROBE_REF_CTR_OFFSET_BITS 32
#define PERF_UPROBE_REF_CTR_OFFSET_SHIFT 32

static int perf_event_open_probe(bool uprobe, bool retprobe, const char *name,
				 uint64_t offset, int pid, size_t ref_ctr_off)
{
	const size_t attr_sz = sizeof(struct perf_event_attr);
	struct perf_event_attr attr;
	char errmsg[STRERR_BUFSIZE];
	int type, pfd;

	if ((__u64)ref_ctr_off >= (1ULL << PERF_UPROBE_REF_CTR_OFFSET_BITS))
		return -EINVAL;

	memset(&attr, 0, attr_sz);

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
	attr.size = attr_sz;
	attr.type = type;
	attr.config |= (__u64)ref_ctr_off << PERF_UPROBE_REF_CTR_OFFSET_SHIFT;
	attr.config1 = ptr_to_u64(name); /* kprobe_func or uprobe_path */
	attr.config2 = offset;		 /* kprobe_addr or probe_offset */

	/* pid filter is meaningful only for uprobes */
	pfd = syscall(__NR_perf_event_open, &attr,
		      pid < 0 ? -1 : pid /* pid */,
		      pid == -1 ? 0 : -1 /* cpu */,
		      -1 /* group_fd */, PERF_FLAG_FD_CLOEXEC);
	return pfd >= 0 ? pfd : -errno;
}

static int append_to_file(const char *file, const char *fmt, ...)
{
	int fd, n, err = 0;
	va_list ap;
	char buf[1024];

	va_start(ap, fmt);
	n = vsnprintf(buf, sizeof(buf), fmt, ap);
	va_end(ap);

	if (n < 0 || n >= sizeof(buf))
		return -EINVAL;

	fd = open(file, O_WRONLY | O_APPEND | O_CLOEXEC, 0);
	if (fd < 0)
		return -errno;

	if (write(fd, buf, n) < 0)
		err = -errno;

	close(fd);
	return err;
}

#define DEBUGFS "/sys/kernel/debug/tracing"
#define TRACEFS "/sys/kernel/tracing"

static bool use_debugfs(void)
{
	static int has_debugfs = -1;

	if (has_debugfs < 0)
		has_debugfs = faccessat(AT_FDCWD, DEBUGFS, F_OK, AT_EACCESS) == 0;

	return has_debugfs == 1;
}

static const char *tracefs_path(void)
{
	return use_debugfs() ? DEBUGFS : TRACEFS;
}

static const char *tracefs_kprobe_events(void)
{
	return use_debugfs() ? DEBUGFS"/kprobe_events" : TRACEFS"/kprobe_events";
}

static const char *tracefs_uprobe_events(void)
{
	return use_debugfs() ? DEBUGFS"/uprobe_events" : TRACEFS"/uprobe_events";
}

static const char *tracefs_available_filter_functions(void)
{
	return use_debugfs() ? DEBUGFS"/available_filter_functions"
			     : TRACEFS"/available_filter_functions";
}

static const char *tracefs_available_filter_functions_addrs(void)
{
	return use_debugfs() ? DEBUGFS"/available_filter_functions_addrs"
			     : TRACEFS"/available_filter_functions_addrs";
}

static void gen_kprobe_legacy_event_name(char *buf, size_t buf_sz,
					 const char *kfunc_name, size_t offset)
{
	static int index = 0;
	int i;

	snprintf(buf, buf_sz, "libbpf_%u_%s_0x%zx_%d", getpid(), kfunc_name, offset,
		 __sync_fetch_and_add(&index, 1));

	/* sanitize binary_path in the probe name */
	for (i = 0; buf[i]; i++) {
		if (!isalnum(buf[i]))
			buf[i] = '_';
	}
}

static int add_kprobe_event_legacy(const char *probe_name, bool retprobe,
				   const char *kfunc_name, size_t offset)
{
	return append_to_file(tracefs_kprobe_events(), "%c:%s/%s %s+0x%zx",
			      retprobe ? 'r' : 'p',
			      retprobe ? "kretprobes" : "kprobes",
			      probe_name, kfunc_name, offset);
}

static int remove_kprobe_event_legacy(const char *probe_name, bool retprobe)
{
	return append_to_file(tracefs_kprobe_events(), "-:%s/%s",
			      retprobe ? "kretprobes" : "kprobes", probe_name);
}

static int determine_kprobe_perf_type_legacy(const char *probe_name, bool retprobe)
{
	char file[256];

	snprintf(file, sizeof(file), "%s/events/%s/%s/id",
		 tracefs_path(), retprobe ? "kretprobes" : "kprobes", probe_name);

	return parse_uint_from_file(file, "%d\n");
}

static int perf_event_kprobe_open_legacy(const char *probe_name, bool retprobe,
					 const char *kfunc_name, size_t offset, int pid)
{
	const size_t attr_sz = sizeof(struct perf_event_attr);
	struct perf_event_attr attr;
	char errmsg[STRERR_BUFSIZE];
	int type, pfd, err;

	err = add_kprobe_event_legacy(probe_name, retprobe, kfunc_name, offset);
	if (err < 0) {
		pr_warn("failed to add legacy kprobe event for '%s+0x%zx': %s\n",
			kfunc_name, offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		return err;
	}
	type = determine_kprobe_perf_type_legacy(probe_name, retprobe);
	if (type < 0) {
		err = type;
		pr_warn("failed to determine legacy kprobe event id for '%s+0x%zx': %s\n",
			kfunc_name, offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto err_clean_legacy;
	}

	memset(&attr, 0, attr_sz);
	attr.size = attr_sz;
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
		goto err_clean_legacy;
	}
	return pfd;

err_clean_legacy:
	/* Clear the newly added legacy kprobe_event */
	remove_kprobe_event_legacy(probe_name, retprobe);
	return err;
}

static const char *arch_specific_syscall_pfx(void)
{
#if defined(__x86_64__)
	return "x64";
#elif defined(__i386__)
	return "ia32";
#elif defined(__s390x__)
	return "s390x";
#elif defined(__s390__)
	return "s390";
#elif defined(__arm__)
	return "arm";
#elif defined(__aarch64__)
	return "arm64";
#elif defined(__mips__)
	return "mips";
#elif defined(__riscv)
	return "riscv";
#elif defined(__powerpc__)
	return "powerpc";
#elif defined(__powerpc64__)
	return "powerpc64";
#else
	return NULL;
#endif
}

int probe_kern_syscall_wrapper(int token_fd)
{
	char syscall_name[64];
	const char *ksys_pfx;

	ksys_pfx = arch_specific_syscall_pfx();
	if (!ksys_pfx)
		return 0;

	snprintf(syscall_name, sizeof(syscall_name), "__%s_sys_bpf", ksys_pfx);

	if (determine_kprobe_perf_type() >= 0) {
		int pfd;

		pfd = perf_event_open_probe(false, false, syscall_name, 0, getpid(), 0);
		if (pfd >= 0)
			close(pfd);

		return pfd >= 0 ? 1 : 0;
	} else { /* legacy mode */
		char probe_name[128];

		gen_kprobe_legacy_event_name(probe_name, sizeof(probe_name), syscall_name, 0);
		if (add_kprobe_event_legacy(probe_name, false, syscall_name, 0) < 0)
			return 0;

		(void)remove_kprobe_event_legacy(probe_name, false);
		return 1;
	}
}

struct bpf_link *
bpf_program__attach_kprobe_opts(const struct bpf_program *prog,
				const char *func_name,
				const struct bpf_kprobe_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, pe_opts);
	enum probe_attach_mode attach_mode;
	char errmsg[STRERR_BUFSIZE];
	char *legacy_probe = NULL;
	struct bpf_link *link;
	size_t offset;
	bool retprobe, legacy;
	int pfd, err;

	if (!OPTS_VALID(opts, bpf_kprobe_opts))
		return libbpf_err_ptr(-EINVAL);

	attach_mode = OPTS_GET(opts, attach_mode, PROBE_ATTACH_MODE_DEFAULT);
	retprobe = OPTS_GET(opts, retprobe, false);
	offset = OPTS_GET(opts, offset, 0);
	pe_opts.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0);

	legacy = determine_kprobe_perf_type() < 0;
	switch (attach_mode) {
	case PROBE_ATTACH_MODE_LEGACY:
		legacy = true;
		pe_opts.force_ioctl_attach = true;
		break;
	case PROBE_ATTACH_MODE_PERF:
		if (legacy)
			return libbpf_err_ptr(-ENOTSUP);
		pe_opts.force_ioctl_attach = true;
		break;
	case PROBE_ATTACH_MODE_LINK:
		if (legacy || !kernel_supports(prog->obj, FEAT_PERF_LINK))
			return libbpf_err_ptr(-ENOTSUP);
		break;
	case PROBE_ATTACH_MODE_DEFAULT:
		break;
	default:
		return libbpf_err_ptr(-EINVAL);
	}

	if (!legacy) {
		pfd = perf_event_open_probe(false /* uprobe */, retprobe,
					    func_name, offset,
					    -1 /* pid */, 0 /* ref_ctr_off */);
	} else {
		char probe_name[256];

		gen_kprobe_legacy_event_name(probe_name, sizeof(probe_name),
					     func_name, offset);

		legacy_probe = strdup(probe_name);
		if (!legacy_probe)
			return libbpf_err_ptr(-ENOMEM);

		pfd = perf_event_kprobe_open_legacy(legacy_probe, retprobe, func_name,
						    offset, -1 /* pid */);
	}
	if (pfd < 0) {
		err = -errno;
		pr_warn("prog '%s': failed to create %s '%s+0x%zx' perf event: %s\n",
			prog->name, retprobe ? "kretprobe" : "kprobe",
			func_name, offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto err_out;
	}
	link = bpf_program__attach_perf_event_opts(prog, pfd, &pe_opts);
	err = libbpf_get_error(link);
	if (err) {
		close(pfd);
		pr_warn("prog '%s': failed to attach to %s '%s+0x%zx': %s\n",
			prog->name, retprobe ? "kretprobe" : "kprobe",
			func_name, offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto err_clean_legacy;
	}
	if (legacy) {
		struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);

		perf_link->legacy_probe_name = legacy_probe;
		perf_link->legacy_is_kprobe = true;
		perf_link->legacy_is_retprobe = retprobe;
	}

	return link;

err_clean_legacy:
	if (legacy)
		remove_kprobe_event_legacy(legacy_probe, retprobe);
err_out:
	free(legacy_probe);
	return libbpf_err_ptr(err);
}

struct bpf_link *bpf_program__attach_kprobe(const struct bpf_program *prog,
					    bool retprobe,
					    const char *func_name)
{
	DECLARE_LIBBPF_OPTS(bpf_kprobe_opts, opts,
		.retprobe = retprobe,
	);

	return bpf_program__attach_kprobe_opts(prog, func_name, &opts);
}

struct bpf_link *bpf_program__attach_ksyscall(const struct bpf_program *prog,
					      const char *syscall_name,
					      const struct bpf_ksyscall_opts *opts)
{
	LIBBPF_OPTS(bpf_kprobe_opts, kprobe_opts);
	char func_name[128];

	if (!OPTS_VALID(opts, bpf_ksyscall_opts))
		return libbpf_err_ptr(-EINVAL);

	if (kernel_supports(prog->obj, FEAT_SYSCALL_WRAPPER)) {
		/* arch_specific_syscall_pfx() should never return NULL here
		 * because it is guarded by kernel_supports(). However, since
		 * compiler does not know that we have an explicit conditional
		 * as well.
		 */
		snprintf(func_name, sizeof(func_name), "__%s_sys_%s",
			 arch_specific_syscall_pfx() ? : "", syscall_name);
	} else {
		snprintf(func_name, sizeof(func_name), "__se_sys_%s", syscall_name);
	}

	kprobe_opts.retprobe = OPTS_GET(opts, retprobe, false);
	kprobe_opts.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0);

	return bpf_program__attach_kprobe_opts(prog, func_name, &kprobe_opts);
}

/* Adapted from perf/util/string.c */
bool glob_match(const char *str, const char *pat)
{
	while (*str && *pat && *pat != '*') {
		if (*pat == '?') {      /* Matches any single character */
			str++;
			pat++;
			continue;
		}
		if (*str != *pat)
			return false;
		str++;
		pat++;
	}
	/* Check wild card */
	if (*pat == '*') {
		while (*pat == '*')
			pat++;
		if (!*pat) /* Tail wild card matches all */
			return true;
		while (*str)
			if (glob_match(str++, pat))
				return true;
	}
	return !*str && !*pat;
}

struct kprobe_multi_resolve {
	const char *pattern;
	unsigned long *addrs;
	size_t cap;
	size_t cnt;
};

struct avail_kallsyms_data {
	char **syms;
	size_t cnt;
	struct kprobe_multi_resolve *res;
};

static int avail_func_cmp(const void *a, const void *b)
{
	return strcmp(*(const char **)a, *(const char **)b);
}

static int avail_kallsyms_cb(unsigned long long sym_addr, char sym_type,
			     const char *sym_name, void *ctx)
{
	struct avail_kallsyms_data *data = ctx;
	struct kprobe_multi_resolve *res = data->res;
	int err;

	if (!bsearch(&sym_name, data->syms, data->cnt, sizeof(*data->syms), avail_func_cmp))
		return 0;

	err = libbpf_ensure_mem((void **)&res->addrs, &res->cap, sizeof(*res->addrs), res->cnt + 1);
	if (err)
		return err;

	res->addrs[res->cnt++] = (unsigned long)sym_addr;
	return 0;
}

static int libbpf_available_kallsyms_parse(struct kprobe_multi_resolve *res)
{
	const char *available_functions_file = tracefs_available_filter_functions();
	struct avail_kallsyms_data data;
	char sym_name[500];
	FILE *f;
	int err = 0, ret, i;
	char **syms = NULL;
	size_t cap = 0, cnt = 0;

	f = fopen(available_functions_file, "re");
	if (!f) {
		err = -errno;
		pr_warn("failed to open %s: %d\n", available_functions_file, err);
		return err;
	}

	while (true) {
		char *name;

		ret = fscanf(f, "%499s%*[^\n]\n", sym_name);
		if (ret == EOF && feof(f))
			break;

		if (ret != 1) {
			pr_warn("failed to parse available_filter_functions entry: %d\n", ret);
			err = -EINVAL;
			goto cleanup;
		}

		if (!glob_match(sym_name, res->pattern))
			continue;

		err = libbpf_ensure_mem((void **)&syms, &cap, sizeof(*syms), cnt + 1);
		if (err)
			goto cleanup;

		name = strdup(sym_name);
		if (!name) {
			err = -errno;
			goto cleanup;
		}

		syms[cnt++] = name;
	}

	/* no entries found, bail out */
	if (cnt == 0) {
		err = -ENOENT;
		goto cleanup;
	}

	/* sort available functions */
	qsort(syms, cnt, sizeof(*syms), avail_func_cmp);

	data.syms = syms;
	data.res = res;
	data.cnt = cnt;
	libbpf_kallsyms_parse(avail_kallsyms_cb, &data);

	if (res->cnt == 0)
		err = -ENOENT;

cleanup:
	for (i = 0; i < cnt; i++)
		free((char *)syms[i]);
	free(syms);

	fclose(f);
	return err;
}

static bool has_available_filter_functions_addrs(void)
{
	return access(tracefs_available_filter_functions_addrs(), R_OK) != -1;
}

static int libbpf_available_kprobes_parse(struct kprobe_multi_resolve *res)
{
	const char *available_path = tracefs_available_filter_functions_addrs();
	char sym_name[500];
	FILE *f;
	int ret, err = 0;
	unsigned long long sym_addr;

	f = fopen(available_path, "re");
	if (!f) {
		err = -errno;
		pr_warn("failed to open %s: %d\n", available_path, err);
		return err;
	}

	while (true) {
		ret = fscanf(f, "%llx %499s%*[^\n]\n", &sym_addr, sym_name);
		if (ret == EOF && feof(f))
			break;

		if (ret != 2) {
			pr_warn("failed to parse available_filter_functions_addrs entry: %d\n",
				ret);
			err = -EINVAL;
			goto cleanup;
		}

		if (!glob_match(sym_name, res->pattern))
			continue;

		err = libbpf_ensure_mem((void **)&res->addrs, &res->cap,
					sizeof(*res->addrs), res->cnt + 1);
		if (err)
			goto cleanup;

		res->addrs[res->cnt++] = (unsigned long)sym_addr;
	}

	if (res->cnt == 0)
		err = -ENOENT;

cleanup:
	fclose(f);
	return err;
}

struct bpf_link *
bpf_program__attach_kprobe_multi_opts(const struct bpf_program *prog,
				      const char *pattern,
				      const struct bpf_kprobe_multi_opts *opts)
{
	LIBBPF_OPTS(bpf_link_create_opts, lopts);
	struct kprobe_multi_resolve res = {
		.pattern = pattern,
	};
	enum bpf_attach_type attach_type;
	struct bpf_link *link = NULL;
	char errmsg[STRERR_BUFSIZE];
	const unsigned long *addrs;
	int err, link_fd, prog_fd;
	bool retprobe, session;
	const __u64 *cookies;
	const char **syms;
	size_t cnt;

	if (!OPTS_VALID(opts, bpf_kprobe_multi_opts))
		return libbpf_err_ptr(-EINVAL);

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach BPF program without FD (was it loaded?)\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	syms    = OPTS_GET(opts, syms, false);
	addrs   = OPTS_GET(opts, addrs, false);
	cnt     = OPTS_GET(opts, cnt, false);
	cookies = OPTS_GET(opts, cookies, false);

	if (!pattern && !addrs && !syms)
		return libbpf_err_ptr(-EINVAL);
	if (pattern && (addrs || syms || cookies || cnt))
		return libbpf_err_ptr(-EINVAL);
	if (!pattern && !cnt)
		return libbpf_err_ptr(-EINVAL);
	if (addrs && syms)
		return libbpf_err_ptr(-EINVAL);

	if (pattern) {
		if (has_available_filter_functions_addrs())
			err = libbpf_available_kprobes_parse(&res);
		else
			err = libbpf_available_kallsyms_parse(&res);
		if (err)
			goto error;
		addrs = res.addrs;
		cnt = res.cnt;
	}

	retprobe = OPTS_GET(opts, retprobe, false);
	session  = OPTS_GET(opts, session, false);

	if (retprobe && session)
		return libbpf_err_ptr(-EINVAL);

	attach_type = session ? BPF_TRACE_KPROBE_SESSION : BPF_TRACE_KPROBE_MULTI;

	lopts.kprobe_multi.syms = syms;
	lopts.kprobe_multi.addrs = addrs;
	lopts.kprobe_multi.cookies = cookies;
	lopts.kprobe_multi.cnt = cnt;
	lopts.kprobe_multi.flags = retprobe ? BPF_F_KPROBE_MULTI_RETURN : 0;

	link = calloc(1, sizeof(*link));
	if (!link) {
		err = -ENOMEM;
		goto error;
	}
	link->detach = &bpf_link__detach_fd;

	link_fd = bpf_link_create(prog_fd, 0, attach_type, &lopts);
	if (link_fd < 0) {
		err = -errno;
		pr_warn("prog '%s': failed to attach: %s\n",
			prog->name, libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto error;
	}
	link->fd = link_fd;
	free(res.addrs);
	return link;

error:
	free(link);
	free(res.addrs);
	return libbpf_err_ptr(err);
}

static int attach_kprobe(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	DECLARE_LIBBPF_OPTS(bpf_kprobe_opts, opts);
	unsigned long offset = 0;
	const char *func_name;
	char *func;
	int n;

	*link = NULL;

	/* no auto-attach for SEC("kprobe") and SEC("kretprobe") */
	if (strcmp(prog->sec_name, "kprobe") == 0 || strcmp(prog->sec_name, "kretprobe") == 0)
		return 0;

	opts.retprobe = str_has_pfx(prog->sec_name, "kretprobe/");
	if (opts.retprobe)
		func_name = prog->sec_name + sizeof("kretprobe/") - 1;
	else
		func_name = prog->sec_name + sizeof("kprobe/") - 1;

	n = sscanf(func_name, "%m[a-zA-Z0-9_.]+%li", &func, &offset);
	if (n < 1) {
		pr_warn("kprobe name is invalid: %s\n", func_name);
		return -EINVAL;
	}
	if (opts.retprobe && offset != 0) {
		free(func);
		pr_warn("kretprobes do not support offset specification\n");
		return -EINVAL;
	}

	opts.offset = offset;
	*link = bpf_program__attach_kprobe_opts(prog, func, &opts);
	free(func);
	return libbpf_get_error(*link);
}

static int attach_ksyscall(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	LIBBPF_OPTS(bpf_ksyscall_opts, opts);
	const char *syscall_name;

	*link = NULL;

	/* no auto-attach for SEC("ksyscall") and SEC("kretsyscall") */
	if (strcmp(prog->sec_name, "ksyscall") == 0 || strcmp(prog->sec_name, "kretsyscall") == 0)
		return 0;

	opts.retprobe = str_has_pfx(prog->sec_name, "kretsyscall/");
	if (opts.retprobe)
		syscall_name = prog->sec_name + sizeof("kretsyscall/") - 1;
	else
		syscall_name = prog->sec_name + sizeof("ksyscall/") - 1;

	*link = bpf_program__attach_ksyscall(prog, syscall_name, &opts);
	return *link ? 0 : -errno;
}

static int attach_kprobe_multi(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts);
	const char *spec;
	char *pattern;
	int n;

	*link = NULL;

	/* no auto-attach for SEC("kprobe.multi") and SEC("kretprobe.multi") */
	if (strcmp(prog->sec_name, "kprobe.multi") == 0 ||
	    strcmp(prog->sec_name, "kretprobe.multi") == 0)
		return 0;

	opts.retprobe = str_has_pfx(prog->sec_name, "kretprobe.multi/");
	if (opts.retprobe)
		spec = prog->sec_name + sizeof("kretprobe.multi/") - 1;
	else
		spec = prog->sec_name + sizeof("kprobe.multi/") - 1;

	n = sscanf(spec, "%m[a-zA-Z0-9_.*?]", &pattern);
	if (n < 1) {
		pr_warn("kprobe multi pattern is invalid: %s\n", pattern);
		return -EINVAL;
	}

	*link = bpf_program__attach_kprobe_multi_opts(prog, pattern, &opts);
	free(pattern);
	return libbpf_get_error(*link);
}

static int attach_kprobe_session(const struct bpf_program *prog, long cookie,
				 struct bpf_link **link)
{
	LIBBPF_OPTS(bpf_kprobe_multi_opts, opts, .session = true);
	const char *spec;
	char *pattern;
	int n;

	*link = NULL;

	/* no auto-attach for SEC("kprobe.session") */
	if (strcmp(prog->sec_name, "kprobe.session") == 0)
		return 0;

	spec = prog->sec_name + sizeof("kprobe.session/") - 1;
	n = sscanf(spec, "%m[a-zA-Z0-9_.*?]", &pattern);
	if (n < 1) {
		pr_warn("kprobe session pattern is invalid: %s\n", pattern);
		return -EINVAL;
	}

	*link = bpf_program__attach_kprobe_multi_opts(prog, pattern, &opts);
	free(pattern);
	return *link ? 0 : -errno;
}

static int attach_uprobe_multi(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	char *probe_type = NULL, *binary_path = NULL, *func_name = NULL;
	LIBBPF_OPTS(bpf_uprobe_multi_opts, opts);
	int n, ret = -EINVAL;

	*link = NULL;

	n = sscanf(prog->sec_name, "%m[^/]/%m[^:]:%m[^\n]",
		   &probe_type, &binary_path, &func_name);
	switch (n) {
	case 1:
		/* handle SEC("u[ret]probe") - format is valid, but auto-attach is impossible. */
		ret = 0;
		break;
	case 3:
		opts.retprobe = strcmp(probe_type, "uretprobe.multi") == 0;
		*link = bpf_program__attach_uprobe_multi(prog, -1, binary_path, func_name, &opts);
		ret = libbpf_get_error(*link);
		break;
	default:
		pr_warn("prog '%s': invalid format of section definition '%s'\n", prog->name,
			prog->sec_name);
		break;
	}
	free(probe_type);
	free(binary_path);
	free(func_name);
	return ret;
}

static void gen_uprobe_legacy_event_name(char *buf, size_t buf_sz,
					 const char *binary_path, uint64_t offset)
{
	int i;

	snprintf(buf, buf_sz, "libbpf_%u_%s_0x%zx", getpid(), binary_path, (size_t)offset);

	/* sanitize binary_path in the probe name */
	for (i = 0; buf[i]; i++) {
		if (!isalnum(buf[i]))
			buf[i] = '_';
	}
}

static inline int add_uprobe_event_legacy(const char *probe_name, bool retprobe,
					  const char *binary_path, size_t offset)
{
	return append_to_file(tracefs_uprobe_events(), "%c:%s/%s %s:0x%zx",
			      retprobe ? 'r' : 'p',
			      retprobe ? "uretprobes" : "uprobes",
			      probe_name, binary_path, offset);
}

static inline int remove_uprobe_event_legacy(const char *probe_name, bool retprobe)
{
	return append_to_file(tracefs_uprobe_events(), "-:%s/%s",
			      retprobe ? "uretprobes" : "uprobes", probe_name);
}

static int determine_uprobe_perf_type_legacy(const char *probe_name, bool retprobe)
{
	char file[512];

	snprintf(file, sizeof(file), "%s/events/%s/%s/id",
		 tracefs_path(), retprobe ? "uretprobes" : "uprobes", probe_name);

	return parse_uint_from_file(file, "%d\n");
}

static int perf_event_uprobe_open_legacy(const char *probe_name, bool retprobe,
					 const char *binary_path, size_t offset, int pid)
{
	const size_t attr_sz = sizeof(struct perf_event_attr);
	struct perf_event_attr attr;
	int type, pfd, err;

	err = add_uprobe_event_legacy(probe_name, retprobe, binary_path, offset);
	if (err < 0) {
		pr_warn("failed to add legacy uprobe event for %s:0x%zx: %d\n",
			binary_path, (size_t)offset, err);
		return err;
	}
	type = determine_uprobe_perf_type_legacy(probe_name, retprobe);
	if (type < 0) {
		err = type;
		pr_warn("failed to determine legacy uprobe event id for %s:0x%zx: %d\n",
			binary_path, offset, err);
		goto err_clean_legacy;
	}

	memset(&attr, 0, attr_sz);
	attr.size = attr_sz;
	attr.config = type;
	attr.type = PERF_TYPE_TRACEPOINT;

	pfd = syscall(__NR_perf_event_open, &attr,
		      pid < 0 ? -1 : pid, /* pid */
		      pid == -1 ? 0 : -1, /* cpu */
		      -1 /* group_fd */,  PERF_FLAG_FD_CLOEXEC);
	if (pfd < 0) {
		err = -errno;
		pr_warn("legacy uprobe perf_event_open() failed: %d\n", err);
		goto err_clean_legacy;
	}
	return pfd;

err_clean_legacy:
	/* Clear the newly added legacy uprobe_event */
	remove_uprobe_event_legacy(probe_name, retprobe);
	return err;
}

/* Find offset of function name in archive specified by path. Currently
 * supported are .zip files that do not compress their contents, as used on
 * Android in the form of APKs, for example. "file_name" is the name of the ELF
 * file inside the archive. "func_name" matches symbol name or name@@LIB for
 * library functions.
 *
 * An overview of the APK format specifically provided here:
 * https://en.wikipedia.org/w/index.php?title=Apk_(file_format)&oldid=1139099120#Package_contents
 */
static long elf_find_func_offset_from_archive(const char *archive_path, const char *file_name,
					      const char *func_name)
{
	struct zip_archive *archive;
	struct zip_entry entry;
	long ret;
	Elf *elf;

	archive = zip_archive_open(archive_path);
	if (IS_ERR(archive)) {
		ret = PTR_ERR(archive);
		pr_warn("zip: failed to open %s: %ld\n", archive_path, ret);
		return ret;
	}

	ret = zip_archive_find_entry(archive, file_name, &entry);
	if (ret) {
		pr_warn("zip: could not find archive member %s in %s: %ld\n", file_name,
			archive_path, ret);
		goto out;
	}
	pr_debug("zip: found entry for %s in %s at 0x%lx\n", file_name, archive_path,
		 (unsigned long)entry.data_offset);

	if (entry.compression) {
		pr_warn("zip: entry %s of %s is compressed and cannot be handled\n", file_name,
			archive_path);
		ret = -LIBBPF_ERRNO__FORMAT;
		goto out;
	}

	elf = elf_memory((void *)entry.data, entry.data_length);
	if (!elf) {
		pr_warn("elf: could not read elf file %s from %s: %s\n", file_name, archive_path,
			elf_errmsg(-1));
		ret = -LIBBPF_ERRNO__LIBELF;
		goto out;
	}

	ret = elf_find_func_offset(elf, file_name, func_name);
	if (ret > 0) {
		pr_debug("elf: symbol address match for %s of %s in %s: 0x%x + 0x%lx = 0x%lx\n",
			 func_name, file_name, archive_path, entry.data_offset, ret,
			 ret + entry.data_offset);
		ret += entry.data_offset;
	}
	elf_end(elf);

out:
	zip_archive_close(archive);
	return ret;
}

static const char *arch_specific_lib_paths(void)
{
	/*
	 * Based on https://packages.debian.org/sid/libc6.
	 *
	 * Assume that the traced program is built for the same architecture
	 * as libbpf, which should cover the vast majority of cases.
	 */
#if defined(__x86_64__)
	return "/lib/x86_64-linux-gnu";
#elif defined(__i386__)
	return "/lib/i386-linux-gnu";
#elif defined(__s390x__)
	return "/lib/s390x-linux-gnu";
#elif defined(__s390__)
	return "/lib/s390-linux-gnu";
#elif defined(__arm__) && defined(__SOFTFP__)
	return "/lib/arm-linux-gnueabi";
#elif defined(__arm__) && !defined(__SOFTFP__)
	return "/lib/arm-linux-gnueabihf";
#elif defined(__aarch64__)
	return "/lib/aarch64-linux-gnu";
#elif defined(__mips__) && defined(__MIPSEL__) && _MIPS_SZLONG == 64
	return "/lib/mips64el-linux-gnuabi64";
#elif defined(__mips__) && defined(__MIPSEL__) && _MIPS_SZLONG == 32
	return "/lib/mipsel-linux-gnu";
#elif defined(__powerpc64__) && __BYTE_ORDER__ == __ORDER_LITTLE_ENDIAN__
	return "/lib/powerpc64le-linux-gnu";
#elif defined(__sparc__) && defined(__arch64__)
	return "/lib/sparc64-linux-gnu";
#elif defined(__riscv) && __riscv_xlen == 64
	return "/lib/riscv64-linux-gnu";
#else
	return NULL;
#endif
}

/* Get full path to program/shared library. */
static int resolve_full_path(const char *file, char *result, size_t result_sz)
{
	const char *search_paths[3] = {};
	int i, perm;

	if (str_has_sfx(file, ".so") || strstr(file, ".so.")) {
		search_paths[0] = getenv("LD_LIBRARY_PATH");
		search_paths[1] = "/usr/lib64:/usr/lib";
		search_paths[2] = arch_specific_lib_paths();
		perm = R_OK;
	} else {
		search_paths[0] = getenv("PATH");
		search_paths[1] = "/usr/bin:/usr/sbin";
		perm = R_OK | X_OK;
	}

	for (i = 0; i < ARRAY_SIZE(search_paths); i++) {
		const char *s;

		if (!search_paths[i])
			continue;
		for (s = search_paths[i]; s != NULL; s = strchr(s, ':')) {
			char *next_path;
			int seg_len;

			if (s[0] == ':')
				s++;
			next_path = strchr(s, ':');
			seg_len = next_path ? next_path - s : strlen(s);
			if (!seg_len)
				continue;
			snprintf(result, result_sz, "%.*s/%s", seg_len, s, file);
			/* ensure it has required permissions */
			if (faccessat(AT_FDCWD, result, perm, AT_EACCESS) < 0)
				continue;
			pr_debug("resolved '%s' to '%s'\n", file, result);
			return 0;
		}
	}
	return -ENOENT;
}

struct bpf_link *
bpf_program__attach_uprobe_multi(const struct bpf_program *prog,
				 pid_t pid,
				 const char *path,
				 const char *func_pattern,
				 const struct bpf_uprobe_multi_opts *opts)
{
	const unsigned long *ref_ctr_offsets = NULL, *offsets = NULL;
	LIBBPF_OPTS(bpf_link_create_opts, lopts);
	unsigned long *resolved_offsets = NULL;
	int err = 0, link_fd, prog_fd;
	struct bpf_link *link = NULL;
	char errmsg[STRERR_BUFSIZE];
	char full_path[PATH_MAX];
	const __u64 *cookies;
	const char **syms;
	size_t cnt;

	if (!OPTS_VALID(opts, bpf_uprobe_multi_opts))
		return libbpf_err_ptr(-EINVAL);

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach BPF program without FD (was it loaded?)\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	syms = OPTS_GET(opts, syms, NULL);
	offsets = OPTS_GET(opts, offsets, NULL);
	ref_ctr_offsets = OPTS_GET(opts, ref_ctr_offsets, NULL);
	cookies = OPTS_GET(opts, cookies, NULL);
	cnt = OPTS_GET(opts, cnt, 0);

	/*
	 * User can specify 2 mutually exclusive set of inputs:
	 *
	 * 1) use only path/func_pattern/pid arguments
	 *
	 * 2) use path/pid with allowed combinations of:
	 *    syms/offsets/ref_ctr_offsets/cookies/cnt
	 *
	 *    - syms and offsets are mutually exclusive
	 *    - ref_ctr_offsets and cookies are optional
	 *
	 * Any other usage results in error.
	 */

	if (!path)
		return libbpf_err_ptr(-EINVAL);
	if (!func_pattern && cnt == 0)
		return libbpf_err_ptr(-EINVAL);

	if (func_pattern) {
		if (syms || offsets || ref_ctr_offsets || cookies || cnt)
			return libbpf_err_ptr(-EINVAL);
	} else {
		if (!!syms == !!offsets)
			return libbpf_err_ptr(-EINVAL);
	}

	if (func_pattern) {
		if (!strchr(path, '/')) {
			err = resolve_full_path(path, full_path, sizeof(full_path));
			if (err) {
				pr_warn("prog '%s': failed to resolve full path for '%s': %d\n",
					prog->name, path, err);
				return libbpf_err_ptr(err);
			}
			path = full_path;
		}

		err = elf_resolve_pattern_offsets(path, func_pattern,
						  &resolved_offsets, &cnt);
		if (err < 0)
			return libbpf_err_ptr(err);
		offsets = resolved_offsets;
	} else if (syms) {
		err = elf_resolve_syms_offsets(path, cnt, syms, &resolved_offsets, STT_FUNC);
		if (err < 0)
			return libbpf_err_ptr(err);
		offsets = resolved_offsets;
	}

	lopts.uprobe_multi.path = path;
	lopts.uprobe_multi.offsets = offsets;
	lopts.uprobe_multi.ref_ctr_offsets = ref_ctr_offsets;
	lopts.uprobe_multi.cookies = cookies;
	lopts.uprobe_multi.cnt = cnt;
	lopts.uprobe_multi.flags = OPTS_GET(opts, retprobe, false) ? BPF_F_UPROBE_MULTI_RETURN : 0;

	if (pid == 0)
		pid = getpid();
	if (pid > 0)
		lopts.uprobe_multi.pid = pid;

	link = calloc(1, sizeof(*link));
	if (!link) {
		err = -ENOMEM;
		goto error;
	}
	link->detach = &bpf_link__detach_fd;

	link_fd = bpf_link_create(prog_fd, 0, BPF_TRACE_UPROBE_MULTI, &lopts);
	if (link_fd < 0) {
		err = -errno;
		pr_warn("prog '%s': failed to attach multi-uprobe: %s\n",
			prog->name, libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto error;
	}
	link->fd = link_fd;
	free(resolved_offsets);
	return link;

error:
	free(resolved_offsets);
	free(link);
	return libbpf_err_ptr(err);
}

LIBBPF_API struct bpf_link *
bpf_program__attach_uprobe_opts(const struct bpf_program *prog, pid_t pid,
				const char *binary_path, size_t func_offset,
				const struct bpf_uprobe_opts *opts)
{
	const char *archive_path = NULL, *archive_sep = NULL;
	char errmsg[STRERR_BUFSIZE], *legacy_probe = NULL;
	DECLARE_LIBBPF_OPTS(bpf_perf_event_opts, pe_opts);
	enum probe_attach_mode attach_mode;
	char full_path[PATH_MAX];
	struct bpf_link *link;
	size_t ref_ctr_off;
	int pfd, err;
	bool retprobe, legacy;
	const char *func_name;

	if (!OPTS_VALID(opts, bpf_uprobe_opts))
		return libbpf_err_ptr(-EINVAL);

	attach_mode = OPTS_GET(opts, attach_mode, PROBE_ATTACH_MODE_DEFAULT);
	retprobe = OPTS_GET(opts, retprobe, false);
	ref_ctr_off = OPTS_GET(opts, ref_ctr_offset, 0);
	pe_opts.bpf_cookie = OPTS_GET(opts, bpf_cookie, 0);

	if (!binary_path)
		return libbpf_err_ptr(-EINVAL);

	/* Check if "binary_path" refers to an archive. */
	archive_sep = strstr(binary_path, "!/");
	if (archive_sep) {
		full_path[0] = '\0';
		libbpf_strlcpy(full_path, binary_path,
			       min(sizeof(full_path), (size_t)(archive_sep - binary_path + 1)));
		archive_path = full_path;
		binary_path = archive_sep + 2;
	} else if (!strchr(binary_path, '/')) {
		err = resolve_full_path(binary_path, full_path, sizeof(full_path));
		if (err) {
			pr_warn("prog '%s': failed to resolve full path for '%s': %d\n",
				prog->name, binary_path, err);
			return libbpf_err_ptr(err);
		}
		binary_path = full_path;
	}
	func_name = OPTS_GET(opts, func_name, NULL);
	if (func_name) {
		long sym_off;

		if (archive_path) {
			sym_off = elf_find_func_offset_from_archive(archive_path, binary_path,
								    func_name);
			binary_path = archive_path;
		} else {
			sym_off = elf_find_func_offset_from_file(binary_path, func_name);
		}
		if (sym_off < 0)
			return libbpf_err_ptr(sym_off);
		func_offset += sym_off;
	}

	legacy = determine_uprobe_perf_type() < 0;
	switch (attach_mode) {
	case PROBE_ATTACH_MODE_LEGACY:
		legacy = true;
		pe_opts.force_ioctl_attach = true;
		break;
	case PROBE_ATTACH_MODE_PERF:
		if (legacy)
			return libbpf_err_ptr(-ENOTSUP);
		pe_opts.force_ioctl_attach = true;
		break;
	case PROBE_ATTACH_MODE_LINK:
		if (legacy || !kernel_supports(prog->obj, FEAT_PERF_LINK))
			return libbpf_err_ptr(-ENOTSUP);
		break;
	case PROBE_ATTACH_MODE_DEFAULT:
		break;
	default:
		return libbpf_err_ptr(-EINVAL);
	}

	if (!legacy) {
		pfd = perf_event_open_probe(true /* uprobe */, retprobe, binary_path,
					    func_offset, pid, ref_ctr_off);
	} else {
		char probe_name[PATH_MAX + 64];

		if (ref_ctr_off)
			return libbpf_err_ptr(-EINVAL);

		gen_uprobe_legacy_event_name(probe_name, sizeof(probe_name),
					     binary_path, func_offset);

		legacy_probe = strdup(probe_name);
		if (!legacy_probe)
			return libbpf_err_ptr(-ENOMEM);

		pfd = perf_event_uprobe_open_legacy(legacy_probe, retprobe,
						    binary_path, func_offset, pid);
	}
	if (pfd < 0) {
		err = -errno;
		pr_warn("prog '%s': failed to create %s '%s:0x%zx' perf event: %s\n",
			prog->name, retprobe ? "uretprobe" : "uprobe",
			binary_path, func_offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto err_out;
	}

	link = bpf_program__attach_perf_event_opts(prog, pfd, &pe_opts);
	err = libbpf_get_error(link);
	if (err) {
		close(pfd);
		pr_warn("prog '%s': failed to attach to %s '%s:0x%zx': %s\n",
			prog->name, retprobe ? "uretprobe" : "uprobe",
			binary_path, func_offset,
			libbpf_strerror_r(err, errmsg, sizeof(errmsg)));
		goto err_clean_legacy;
	}
	if (legacy) {
		struct bpf_link_perf *perf_link = container_of(link, struct bpf_link_perf, link);

		perf_link->legacy_probe_name = legacy_probe;
		perf_link->legacy_is_kprobe = false;
		perf_link->legacy_is_retprobe = retprobe;
	}
	return link;

err_clean_legacy:
	if (legacy)
		remove_uprobe_event_legacy(legacy_probe, retprobe);
err_out:
	free(legacy_probe);
	return libbpf_err_ptr(err);
}

/* Format of u[ret]probe section definition supporting auto-attach:
 * u[ret]probe/binary:function[+offset]
 *
 * binary can be an absolute/relative path or a filename; the latter is resolved to a
 * full binary path via bpf_program__attach_uprobe_opts.
 *
 * Specifying uprobe+ ensures we carry out strict matching; either "uprobe" must be
 * specified (and auto-attach is not possible) or the above format is specified for
 * auto-attach.
 */
static int attach_uprobe(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, opts);
	char *probe_type = NULL, *binary_path = NULL, *func_name = NULL, *func_off;
	int n, c, ret = -EINVAL;
	long offset = 0;

	*link = NULL;

	n = sscanf(prog->sec_name, "%m[^/]/%m[^:]:%m[^\n]",
		   &probe_type, &binary_path, &func_name);
	switch (n) {
	case 1:
		/* handle SEC("u[ret]probe") - format is valid, but auto-attach is impossible. */
		ret = 0;
		break;
	case 2:
		pr_warn("prog '%s': section '%s' missing ':function[+offset]' specification\n",
			prog->name, prog->sec_name);
		break;
	case 3:
		/* check if user specifies `+offset`, if yes, this should be
		 * the last part of the string, make sure sscanf read to EOL
		 */
		func_off = strrchr(func_name, '+');
		if (func_off) {
			n = sscanf(func_off, "+%li%n", &offset, &c);
			if (n == 1 && *(func_off + c) == '\0')
				func_off[0] = '\0';
			else
				offset = 0;
		}
		opts.retprobe = strcmp(probe_type, "uretprobe") == 0 ||
				strcmp(probe_type, "uretprobe.s") == 0;
		if (opts.retprobe && offset != 0) {
			pr_warn("prog '%s': uretprobes do not support offset specification\n",
				prog->name);
			break;
		}
		opts.func_name = func_name;
		*link = bpf_program__attach_uprobe_opts(prog, -1, binary_path, offset, &opts);
		ret = libbpf_get_error(*link);
		break;
	default:
		pr_warn("prog '%s': invalid format of section definition '%s'\n", prog->name,
			prog->sec_name);
		break;
	}
	free(probe_type);
	free(binary_path);
	free(func_name);

	return ret;
}

struct bpf_link *bpf_program__attach_uprobe(const struct bpf_program *prog,
					    bool retprobe, pid_t pid,
					    const char *binary_path,
					    size_t func_offset)
{
	DECLARE_LIBBPF_OPTS(bpf_uprobe_opts, opts, .retprobe = retprobe);

	return bpf_program__attach_uprobe_opts(prog, pid, binary_path, func_offset, &opts);
}

struct bpf_link *bpf_program__attach_usdt(const struct bpf_program *prog,
					  pid_t pid, const char *binary_path,
					  const char *usdt_provider, const char *usdt_name,
					  const struct bpf_usdt_opts *opts)
{
	char resolved_path[512];
	struct bpf_object *obj = prog->obj;
	struct bpf_link *link;
	__u64 usdt_cookie;
	int err;

	if (!OPTS_VALID(opts, bpf_uprobe_opts))
		return libbpf_err_ptr(-EINVAL);

	if (bpf_program__fd(prog) < 0) {
		pr_warn("prog '%s': can't attach BPF program without FD (was it loaded?)\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	if (!binary_path)
		return libbpf_err_ptr(-EINVAL);

	if (!strchr(binary_path, '/')) {
		err = resolve_full_path(binary_path, resolved_path, sizeof(resolved_path));
		if (err) {
			pr_warn("prog '%s': failed to resolve full path for '%s': %d\n",
				prog->name, binary_path, err);
			return libbpf_err_ptr(err);
		}
		binary_path = resolved_path;
	}

	/* USDT manager is instantiated lazily on first USDT attach. It will
	 * be destroyed together with BPF object in bpf_object__close().
	 */
	if (IS_ERR(obj->usdt_man))
		return libbpf_ptr(obj->usdt_man);
	if (!obj->usdt_man) {
		obj->usdt_man = usdt_manager_new(obj);
		if (IS_ERR(obj->usdt_man))
			return libbpf_ptr(obj->usdt_man);
	}

	usdt_cookie = OPTS_GET(opts, usdt_cookie, 0);
	link = usdt_manager_attach_usdt(obj->usdt_man, prog, pid, binary_path,
					usdt_provider, usdt_name, usdt_cookie);
	err = libbpf_get_error(link);
	if (err)
		return libbpf_err_ptr(err);
	return link;
}

static int attach_usdt(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	char *path = NULL, *provider = NULL, *name = NULL;
	const char *sec_name;
	int n, err;

	sec_name = bpf_program__section_name(prog);
	if (strcmp(sec_name, "usdt") == 0) {
		/* no auto-attach for just SEC("usdt") */
		*link = NULL;
		return 0;
	}

	n = sscanf(sec_name, "usdt/%m[^:]:%m[^:]:%m[^:]", &path, &provider, &name);
	if (n != 3) {
		pr_warn("invalid section '%s', expected SEC(\"usdt/<path>:<provider>:<name>\")\n",
			sec_name);
		err = -EINVAL;
	} else {
		*link = bpf_program__attach_usdt(prog, -1 /* any process */, path,
						 provider, name, NULL);
		err = libbpf_get_error(*link);
	}
	free(path);
	free(provider);
	free(name);
	return err;
}

static int determine_tracepoint_id(const char *tp_category,
				   const char *tp_name)
{
	char file[PATH_MAX];
	int ret;

	ret = snprintf(file, sizeof(file), "%s/events/%s/%s/id",
		       tracefs_path(), tp_category, tp_name);
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
	const size_t attr_sz = sizeof(struct perf_event_attr);
	struct perf_event_attr attr;
	char errmsg[STRERR_BUFSIZE];
	int tp_id, pfd, err;

	tp_id = determine_tracepoint_id(tp_category, tp_name);
	if (tp_id < 0) {
		pr_warn("failed to determine tracepoint '%s/%s' perf event ID: %s\n",
			tp_category, tp_name,
			libbpf_strerror_r(tp_id, errmsg, sizeof(errmsg)));
		return tp_id;
	}

	memset(&attr, 0, attr_sz);
	attr.type = PERF_TYPE_TRACEPOINT;
	attr.size = attr_sz;
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

struct bpf_link *bpf_program__attach_tracepoint_opts(const struct bpf_program *prog,
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

struct bpf_link *bpf_program__attach_tracepoint(const struct bpf_program *prog,
						const char *tp_category,
						const char *tp_name)
{
	return bpf_program__attach_tracepoint_opts(prog, tp_category, tp_name, NULL);
}

static int attach_tp(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	char *sec_name, *tp_cat, *tp_name;

	*link = NULL;

	/* no auto-attach for SEC("tp") or SEC("tracepoint") */
	if (strcmp(prog->sec_name, "tp") == 0 || strcmp(prog->sec_name, "tracepoint") == 0)
		return 0;

	sec_name = strdup(prog->sec_name);
	if (!sec_name)
		return -ENOMEM;

	/* extract "tp/<category>/<name>" or "tracepoint/<category>/<name>" */
	if (str_has_pfx(prog->sec_name, "tp/"))
		tp_cat = sec_name + sizeof("tp/") - 1;
	else
		tp_cat = sec_name + sizeof("tracepoint/") - 1;
	tp_name = strchr(tp_cat, '/');
	if (!tp_name) {
		free(sec_name);
		return -EINVAL;
	}
	*tp_name = '\0';
	tp_name++;

	*link = bpf_program__attach_tracepoint(prog, tp_cat, tp_name);
	free(sec_name);
	return libbpf_get_error(*link);
}

struct bpf_link *
bpf_program__attach_raw_tracepoint_opts(const struct bpf_program *prog,
					const char *tp_name,
					struct bpf_raw_tracepoint_opts *opts)
{
	LIBBPF_OPTS(bpf_raw_tp_opts, raw_opts);
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int prog_fd, pfd;

	if (!OPTS_VALID(opts, bpf_raw_tracepoint_opts))
		return libbpf_err_ptr(-EINVAL);

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->detach = &bpf_link__detach_fd;

	raw_opts.tp_name = tp_name;
	raw_opts.cookie = OPTS_GET(opts, cookie, 0);
	pfd = bpf_raw_tracepoint_open_opts(prog_fd, &raw_opts);
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

struct bpf_link *bpf_program__attach_raw_tracepoint(const struct bpf_program *prog,
						    const char *tp_name)
{
	return bpf_program__attach_raw_tracepoint_opts(prog, tp_name, NULL);
}

static int attach_raw_tp(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	static const char *const prefixes[] = {
		"raw_tp",
		"raw_tracepoint",
		"raw_tp.w",
		"raw_tracepoint.w",
	};
	size_t i;
	const char *tp_name = NULL;

	*link = NULL;

	for (i = 0; i < ARRAY_SIZE(prefixes); i++) {
		size_t pfx_len;

		if (!str_has_pfx(prog->sec_name, prefixes[i]))
			continue;

		pfx_len = strlen(prefixes[i]);
		/* no auto-attach case of, e.g., SEC("raw_tp") */
		if (prog->sec_name[pfx_len] == '\0')
			return 0;

		if (prog->sec_name[pfx_len] != '/')
			continue;

		tp_name = prog->sec_name + pfx_len + 1;
		break;
	}

	if (!tp_name) {
		pr_warn("prog '%s': invalid section name '%s'\n",
			prog->name, prog->sec_name);
		return -EINVAL;
	}

	*link = bpf_program__attach_raw_tracepoint(prog, tp_name);
	return libbpf_get_error(*link);
}

/* Common logic for all BPF program types that attach to a btf_id */
static struct bpf_link *bpf_program__attach_btf_id(const struct bpf_program *prog,
						   const struct bpf_trace_opts *opts)
{
	LIBBPF_OPTS(bpf_link_create_opts, link_opts);
	char errmsg[STRERR_BUFSIZE];
	struct bpf_link *link;
	int prog_fd, pfd;

	if (!OPTS_VALID(opts, bpf_trace_opts))
		return libbpf_err_ptr(-EINVAL);

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);
	link->detach = &bpf_link__detach_fd;

	/* libbpf is smart enough to redirect to BPF_RAW_TRACEPOINT_OPEN on old kernels */
	link_opts.tracing.cookie = OPTS_GET(opts, cookie, 0);
	pfd = bpf_link_create(prog_fd, 0, bpf_program__expected_attach_type(prog), &link_opts);
	if (pfd < 0) {
		pfd = -errno;
		free(link);
		pr_warn("prog '%s': failed to attach: %s\n",
			prog->name, libbpf_strerror_r(pfd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(pfd);
	}
	link->fd = pfd;
	return link;
}

struct bpf_link *bpf_program__attach_trace(const struct bpf_program *prog)
{
	return bpf_program__attach_btf_id(prog, NULL);
}

struct bpf_link *bpf_program__attach_trace_opts(const struct bpf_program *prog,
						const struct bpf_trace_opts *opts)
{
	return bpf_program__attach_btf_id(prog, opts);
}

struct bpf_link *bpf_program__attach_lsm(const struct bpf_program *prog)
{
	return bpf_program__attach_btf_id(prog, NULL);
}

static int attach_trace(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	*link = bpf_program__attach_trace(prog);
	return libbpf_get_error(*link);
}

static int attach_lsm(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	*link = bpf_program__attach_lsm(prog);
	return libbpf_get_error(*link);
}

static struct bpf_link *
bpf_program_attach_fd(const struct bpf_program *prog,
		      int target_fd, const char *target_name,
		      const struct bpf_link_create_opts *opts)
{
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

	attach_type = bpf_program__expected_attach_type(prog);
	link_fd = bpf_link_create(prog_fd, target_fd, attach_type, opts);
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
bpf_program__attach_cgroup(const struct bpf_program *prog, int cgroup_fd)
{
	return bpf_program_attach_fd(prog, cgroup_fd, "cgroup", NULL);
}

struct bpf_link *
bpf_program__attach_netns(const struct bpf_program *prog, int netns_fd)
{
	return bpf_program_attach_fd(prog, netns_fd, "netns", NULL);
}

struct bpf_link *
bpf_program__attach_sockmap(const struct bpf_program *prog, int map_fd)
{
	return bpf_program_attach_fd(prog, map_fd, "sockmap", NULL);
}

struct bpf_link *bpf_program__attach_xdp(const struct bpf_program *prog, int ifindex)
{
	/* target_fd/target_ifindex use the same field in LINK_CREATE */
	return bpf_program_attach_fd(prog, ifindex, "xdp", NULL);
}

struct bpf_link *
bpf_program__attach_tcx(const struct bpf_program *prog, int ifindex,
			const struct bpf_tcx_opts *opts)
{
	LIBBPF_OPTS(bpf_link_create_opts, link_create_opts);
	__u32 relative_id;
	int relative_fd;

	if (!OPTS_VALID(opts, bpf_tcx_opts))
		return libbpf_err_ptr(-EINVAL);

	relative_id = OPTS_GET(opts, relative_id, 0);
	relative_fd = OPTS_GET(opts, relative_fd, 0);

	/* validate we don't have unexpected combinations of non-zero fields */
	if (!ifindex) {
		pr_warn("prog '%s': target netdevice ifindex cannot be zero\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}
	if (relative_fd && relative_id) {
		pr_warn("prog '%s': relative_fd and relative_id cannot be set at the same time\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link_create_opts.tcx.expected_revision = OPTS_GET(opts, expected_revision, 0);
	link_create_opts.tcx.relative_fd = relative_fd;
	link_create_opts.tcx.relative_id = relative_id;
	link_create_opts.flags = OPTS_GET(opts, flags, 0);

	/* target_fd/target_ifindex use the same field in LINK_CREATE */
	return bpf_program_attach_fd(prog, ifindex, "tcx", &link_create_opts);
}

struct bpf_link *
bpf_program__attach_netkit(const struct bpf_program *prog, int ifindex,
			   const struct bpf_netkit_opts *opts)
{
	LIBBPF_OPTS(bpf_link_create_opts, link_create_opts);
	__u32 relative_id;
	int relative_fd;

	if (!OPTS_VALID(opts, bpf_netkit_opts))
		return libbpf_err_ptr(-EINVAL);

	relative_id = OPTS_GET(opts, relative_id, 0);
	relative_fd = OPTS_GET(opts, relative_fd, 0);

	/* validate we don't have unexpected combinations of non-zero fields */
	if (!ifindex) {
		pr_warn("prog '%s': target netdevice ifindex cannot be zero\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}
	if (relative_fd && relative_id) {
		pr_warn("prog '%s': relative_fd and relative_id cannot be set at the same time\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link_create_opts.netkit.expected_revision = OPTS_GET(opts, expected_revision, 0);
	link_create_opts.netkit.relative_fd = relative_fd;
	link_create_opts.netkit.relative_id = relative_id;
	link_create_opts.flags = OPTS_GET(opts, flags, 0);

	return bpf_program_attach_fd(prog, ifindex, "netkit", &link_create_opts);
}

struct bpf_link *bpf_program__attach_freplace(const struct bpf_program *prog,
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
		LIBBPF_OPTS(bpf_link_create_opts, target_opts);

		btf_id = libbpf_find_prog_btf_id(attach_func_name, target_fd);
		if (btf_id < 0)
			return libbpf_err_ptr(btf_id);

		target_opts.target_btf_id = btf_id;

		return bpf_program_attach_fd(prog, target_fd, "freplace",
					     &target_opts);
	} else {
		/* no target, so use raw_tracepoint_open for compatibility
		 * with old kernels
		 */
		return bpf_program__attach_trace(prog);
	}
}

struct bpf_link *
bpf_program__attach_iter(const struct bpf_program *prog,
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

static int attach_iter(const struct bpf_program *prog, long cookie, struct bpf_link **link)
{
	*link = bpf_program__attach_iter(prog, NULL);
	return libbpf_get_error(*link);
}

struct bpf_link *bpf_program__attach_netfilter(const struct bpf_program *prog,
					       const struct bpf_netfilter_opts *opts)
{
	LIBBPF_OPTS(bpf_link_create_opts, lopts);
	struct bpf_link *link;
	int prog_fd, link_fd;

	if (!OPTS_VALID(opts, bpf_netfilter_opts))
		return libbpf_err_ptr(-EINVAL);

	prog_fd = bpf_program__fd(prog);
	if (prog_fd < 0) {
		pr_warn("prog '%s': can't attach before loaded\n", prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-ENOMEM);

	link->detach = &bpf_link__detach_fd;

	lopts.netfilter.pf = OPTS_GET(opts, pf, 0);
	lopts.netfilter.hooknum = OPTS_GET(opts, hooknum, 0);
	lopts.netfilter.priority = OPTS_GET(opts, priority, 0);
	lopts.netfilter.flags = OPTS_GET(opts, flags, 0);

	link_fd = bpf_link_create(prog_fd, 0, BPF_NETFILTER, &lopts);
	if (link_fd < 0) {
		char errmsg[STRERR_BUFSIZE];

		link_fd = -errno;
		free(link);
		pr_warn("prog '%s': failed to attach to netfilter: %s\n",
			prog->name, libbpf_strerror_r(link_fd, errmsg, sizeof(errmsg)));
		return libbpf_err_ptr(link_fd);
	}
	link->fd = link_fd;

	return link;
}

struct bpf_link *bpf_program__attach(const struct bpf_program *prog)
{
	struct bpf_link *link = NULL;
	int err;

	if (!prog->sec_def || !prog->sec_def->prog_attach_fn)
		return libbpf_err_ptr(-EOPNOTSUPP);

	if (bpf_program__fd(prog) < 0) {
		pr_warn("prog '%s': can't attach BPF program without FD (was it loaded?)\n",
			prog->name);
		return libbpf_err_ptr(-EINVAL);
	}

	err = prog->sec_def->prog_attach_fn(prog, prog->sec_def->cookie, &link);
	if (err)
		return libbpf_err_ptr(err);

	/* When calling bpf_program__attach() explicitly, auto-attach support
	 * is expected to work, so NULL returned link is considered an error.
	 * This is different for skeleton's attach, see comment in
	 * bpf_object__attach_skeleton().
	 */
	if (!link)
		return libbpf_err_ptr(-EOPNOTSUPP);

	return link;
}

struct bpf_link_struct_ops {
	struct bpf_link link;
	int map_fd;
};

static int bpf_link__detach_struct_ops(struct bpf_link *link)
{
	struct bpf_link_struct_ops *st_link;
	__u32 zero = 0;

	st_link = container_of(link, struct bpf_link_struct_ops, link);

	if (st_link->map_fd < 0)
		/* w/o a real link */
		return bpf_map_delete_elem(link->fd, &zero);

	return close(link->fd);
}

struct bpf_link *bpf_map__attach_struct_ops(const struct bpf_map *map)
{
	struct bpf_link_struct_ops *link;
	__u32 zero = 0;
	int err, fd;

	if (!bpf_map__is_struct_ops(map))
		return libbpf_err_ptr(-EINVAL);

	if (map->fd < 0) {
		pr_warn("map '%s': can't attach BPF map without FD (was it created?)\n", map->name);
		return libbpf_err_ptr(-EINVAL);
	}

	link = calloc(1, sizeof(*link));
	if (!link)
		return libbpf_err_ptr(-EINVAL);

	/* kern_vdata should be prepared during the loading phase. */
	err = bpf_map_update_elem(map->fd, &zero, map->st_ops->kern_vdata, 0);
	/* It can be EBUSY if the map has been used to create or
	 * update a link before.  We don't allow updating the value of
	 * a struct_ops once it is set.  That ensures that the value
	 * never changed.  So, it is safe to skip EBUSY.
	 */
	if (err && (!(map->def.map_flags & BPF_F_LINK) || err != -EBUSY)) {
		free(link);
		return libbpf_err_ptr(err);
	}

	link->link.detach = bpf_link__detach_struct_ops;

	if (!(map->def.map_flags & BPF_F_LINK)) {
		/* w/o a real link */
		link->link.fd = map->fd;
		link->map_fd = -1;
		return &link->link;
	}

	fd = bpf_link_create(map->fd, 0, BPF_STRUCT_OPS, NULL);
	if (fd < 0) {
		free(link);
		return libbpf_err_ptr(fd);
	}

	link->link.fd = fd;
	link->map_fd = map->fd;

	return &link->link;
}

/*
 * Swap the back struct_ops of a link with a new struct_ops map.
 */
int bpf_link__update_map(struct bpf_link *link, const struct bpf_map *map)
{
	struct bpf_link_struct_ops *st_ops_link;
	__u32 zero = 0;
	int err;

	if (!bpf_map__is_struct_ops(map))
		return -EINVAL;

	if (map->fd < 0) {
		pr_warn("map '%s': can't use BPF map without FD (was it created?)\n", map->name);
		return -EINVAL;
	}

	st_ops_link = container_of(link, struct bpf_link_struct_ops, link);
	/* Ensure the type of a link is correct */
	if (st_ops_link->map_fd < 0)
		return -EINVAL;

	err = bpf_map_update_elem(map->fd, &zero, map->st_ops->kern_vdata, 0);
	/* It can be EBUSY if the map has been used to create or
	 * update a link before.  We don't allow updating the value of
	 * a struct_ops once it is set.  That ensures that the value
	 * never changed.  So, it is safe to skip EBUSY.
	 */
	if (err && err != -EBUSY)
		return err;

	err = bpf_link_update(link->fd, map->fd, NULL);
	if (err < 0)
		return err;

	st_ops_link->map_fd = map->fd;

	return 0;
}

typedef enum bpf_perf_event_ret (*bpf_perf_event_print_t)(struct perf_event_header *hdr,
							  void *private_data);

static enum bpf_perf_event_ret
perf_event_read_simple(void *mmap_mem, size_t mmap_size, size_t page_size,
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
				     perf_buffer_sample_fn sample_cb,
				     perf_buffer_lost_fn lost_cb,
				     void *ctx,
				     const struct perf_buffer_opts *opts)
{
	const size_t attr_sz = sizeof(struct perf_event_attr);
	struct perf_buffer_params p = {};
	struct perf_event_attr attr;
	__u32 sample_period;

	if (!OPTS_VALID(opts, perf_buffer_opts))
		return libbpf_err_ptr(-EINVAL);

	sample_period = OPTS_GET(opts, sample_period, 1);
	if (!sample_period)
		sample_period = 1;

	memset(&attr, 0, attr_sz);
	attr.size = attr_sz;
	attr.config = PERF_COUNT_SW_BPF_OUTPUT;
	attr.type = PERF_TYPE_SOFTWARE;
	attr.sample_type = PERF_SAMPLE_RAW;
	attr.sample_period = sample_period;
	attr.wakeup_events = sample_period;

	p.attr = &attr;
	p.sample_cb = sample_cb;
	p.lost_cb = lost_cb;
	p.ctx = ctx;

	return libbpf_ptr(__perf_buffer__new(map_fd, page_cnt, &p));
}

struct perf_buffer *perf_buffer__new_raw(int map_fd, size_t page_cnt,
					 struct perf_event_attr *attr,
					 perf_buffer_event_fn event_cb, void *ctx,
					 const struct perf_buffer_raw_opts *opts)
{
	struct perf_buffer_params p = {};

	if (!attr)
		return libbpf_err_ptr(-EINVAL);

	if (!OPTS_VALID(opts, perf_buffer_raw_opts))
		return libbpf_err_ptr(-EINVAL);

	p.attr = attr;
	p.event_cb = event_cb;
	p.ctx = ctx;
	p.cpu_cnt = OPTS_GET(opts, cpu_cnt, 0);
	p.cpus = OPTS_GET(opts, cpus, NULL);
	p.map_keys = OPTS_GET(opts, map_keys, NULL);

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

	if (page_cnt == 0 || (page_cnt & (page_cnt - 1))) {
		pr_warn("page count should be power of two, but is %zu\n",
			page_cnt);
		return ERR_PTR(-EINVAL);
	}

	/* best-effort sanity checks */
	memset(&map, 0, sizeof(map));
	map_info_len = sizeof(map);
	err = bpf_map_get_info_by_fd(map_fd, &map, &map_info_len);
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

	ret = perf_event_read_simple(cpu_buf->base, pb->mmap_size,
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

int perf_buffer__buffer(struct perf_buffer *pb, int buf_idx, void **buf, size_t *buf_size)
{
	struct perf_cpu_buf *cpu_buf;

	if (buf_idx >= pb->cpu_cnt)
		return libbpf_err(-EINVAL);

	cpu_buf = pb->cpu_bufs[buf_idx];
	if (!cpu_buf)
		return libbpf_err(-ENOENT);

	*buf = cpu_buf->base;
	*buf_size = pb->mmap_size;
	return 0;
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

int bpf_program__set_attach_target(struct bpf_program *prog,
				   int attach_prog_fd,
				   const char *attach_func_name)
{
	int btf_obj_fd = 0, btf_id = 0, err;

	if (!prog || attach_prog_fd < 0)
		return libbpf_err(-EINVAL);

	if (prog->obj->loaded)
		return libbpf_err(-EINVAL);

	if (attach_prog_fd && !attach_func_name) {
		/* remember attach_prog_fd and let bpf_program__load() find
		 * BTF ID during the program load
		 */
		prog->attach_prog_fd = attach_prog_fd;
		return 0;
	}

	if (attach_prog_fd) {
		btf_id = libbpf_find_prog_btf_id(attach_func_name,
						 attach_prog_fd);
		if (btf_id < 0)
			return libbpf_err(btf_id);
	} else {
		if (!attach_func_name)
			return libbpf_err(-EINVAL);

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

	fd = open(fcpu, O_RDONLY | O_CLOEXEC);
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

static int populate_skeleton_maps(const struct bpf_object *obj,
				  struct bpf_map_skeleton *maps,
				  size_t map_cnt)
{
	int i;

	for (i = 0; i < map_cnt; i++) {
		struct bpf_map **map = maps[i].map;
		const char *name = maps[i].name;
		void **mmaped = maps[i].mmaped;

		*map = bpf_object__find_map_by_name(obj, name);
		if (!*map) {
			pr_warn("failed to find skeleton map '%s'\n", name);
			return -ESRCH;
		}

		/* externs shouldn't be pre-setup from user code */
		if (mmaped && (*map)->libbpf_type != LIBBPF_MAP_KCONFIG)
			*mmaped = (*map)->mmaped;
	}
	return 0;
}

static int populate_skeleton_progs(const struct bpf_object *obj,
				   struct bpf_prog_skeleton *progs,
				   size_t prog_cnt)
{
	int i;

	for (i = 0; i < prog_cnt; i++) {
		struct bpf_program **prog = progs[i].prog;
		const char *name = progs[i].name;

		*prog = bpf_object__find_program_by_name(obj, name);
		if (!*prog) {
			pr_warn("failed to find skeleton program '%s'\n", name);
			return -ESRCH;
		}
	}
	return 0;
}

int bpf_object__open_skeleton(struct bpf_object_skeleton *s,
			      const struct bpf_object_open_opts *opts)
{
	DECLARE_LIBBPF_OPTS(bpf_object_open_opts, skel_opts,
		.object_name = s->name,
	);
	struct bpf_object *obj;
	int err;

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
	err = populate_skeleton_maps(obj, s->maps, s->map_cnt);
	if (err) {
		pr_warn("failed to populate skeleton maps for '%s': %d\n", s->name, err);
		return libbpf_err(err);
	}

	err = populate_skeleton_progs(obj, s->progs, s->prog_cnt);
	if (err) {
		pr_warn("failed to populate skeleton progs for '%s': %d\n", s->name, err);
		return libbpf_err(err);
	}

	return 0;
}

int bpf_object__open_subskeleton(struct bpf_object_subskeleton *s)
{
	int err, len, var_idx, i;
	const char *var_name;
	const struct bpf_map *map;
	struct btf *btf;
	__u32 map_type_id;
	const struct btf_type *map_type, *var_type;
	const struct bpf_var_skeleton *var_skel;
	struct btf_var_secinfo *var;

	if (!s->obj)
		return libbpf_err(-EINVAL);

	btf = bpf_object__btf(s->obj);
	if (!btf) {
		pr_warn("subskeletons require BTF at runtime (object %s)\n",
			bpf_object__name(s->obj));
		return libbpf_err(-errno);
	}

	err = populate_skeleton_maps(s->obj, s->maps, s->map_cnt);
	if (err) {
		pr_warn("failed to populate subskeleton maps: %d\n", err);
		return libbpf_err(err);
	}

	err = populate_skeleton_progs(s->obj, s->progs, s->prog_cnt);
	if (err) {
		pr_warn("failed to populate subskeleton maps: %d\n", err);
		return libbpf_err(err);
	}

	for (var_idx = 0; var_idx < s->var_cnt; var_idx++) {
		var_skel = &s->vars[var_idx];
		map = *var_skel->map;
		map_type_id = bpf_map__btf_value_type_id(map);
		map_type = btf__type_by_id(btf, map_type_id);

		if (!btf_is_datasec(map_type)) {
			pr_warn("type for map '%1$s' is not a datasec: %2$s",
				bpf_map__name(map),
				__btf_kind_str(btf_kind(map_type)));
			return libbpf_err(-EINVAL);
		}

		len = btf_vlen(map_type);
		var = btf_var_secinfos(map_type);
		for (i = 0; i < len; i++, var++) {
			var_type = btf__type_by_id(btf, var->type);
			var_name = btf__name_by_offset(btf, var_type->name_off);
			if (strcmp(var_name, var_skel->name) == 0) {
				*var_skel->addr = map->mmaped + var->offset;
				break;
			}
		}
	}
	return 0;
}

void bpf_object__destroy_subskeleton(struct bpf_object_subskeleton *s)
{
	if (!s)
		return;
	free(s->maps);
	free(s->progs);
	free(s->vars);
	free(s);
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
		int prot, map_fd = map->fd;
		void **mmaped = s->maps[i].mmaped;

		if (!mmaped)
			continue;

		if (!(map->def.map_flags & BPF_F_MMAPABLE)) {
			*mmaped = NULL;
			continue;
		}

		if (map->def.type == BPF_MAP_TYPE_ARENA) {
			*mmaped = map->mmaped;
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
		*mmaped = mmap(map->mmaped, mmap_sz, prot, MAP_SHARED | MAP_FIXED, map_fd, 0);
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

		if (!prog->autoload || !prog->autoattach)
			continue;

		/* auto-attaching not supported for this program */
		if (!prog->sec_def || !prog->sec_def->prog_attach_fn)
			continue;

		/* if user already set the link manually, don't attempt auto-attach */
		if (*link)
			continue;

		err = prog->sec_def->prog_attach_fn(prog, prog->sec_def->cookie, link);
		if (err) {
			pr_warn("prog '%s': failed to auto-attach: %d\n",
				bpf_program__name(prog), err);
			return libbpf_err(err);
		}

		/* It's possible that for some SEC() definitions auto-attach
		 * is supported in some cases (e.g., if definition completely
		 * specifies target information), but is not in other cases.
		 * SEC("uprobe") is one such case. If user specified target
		 * binary and function name, such BPF program can be
		 * auto-attached. But if not, it shouldn't trigger skeleton's
		 * attach to fail. It should just be skipped.
		 * attach_fn signals such case with returning 0 (no error) and
		 * setting link to NULL.
		 */
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
	if (!s)
		return;

	if (s->progs)
		bpf_object__detach_skeleton(s);
	if (s->obj)
		bpf_object__close(*s->obj);
	free(s->maps);
	free(s->progs);
	free(s);
}
