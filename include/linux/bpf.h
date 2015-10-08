/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of version 2 of the GNU General Public
 * License as published by the Free Software Foundation.
 */
#ifndef _LINUX_BPF_H
#define _LINUX_BPF_H 1

#include <uapi/linux/bpf.h>
#include <linux/workqueue.h>
#include <linux/file.h>

struct bpf_map;

/* map is generic key/value storage optionally accesible by eBPF programs */
struct bpf_map_ops {
	/* funcs callable from userspace (via syscall) */
	struct bpf_map *(*map_alloc)(union bpf_attr *attr);
	void (*map_free)(struct bpf_map *);
	int (*map_get_next_key)(struct bpf_map *map, void *key, void *next_key);

	/* funcs callable from userspace and from eBPF programs */
	void *(*map_lookup_elem)(struct bpf_map *map, void *key);
	int (*map_update_elem)(struct bpf_map *map, void *key, void *value, u64 flags);
	int (*map_delete_elem)(struct bpf_map *map, void *key);

	/* funcs called by prog_array and perf_event_array map */
	void *(*map_fd_get_ptr) (struct bpf_map *map, int fd);
	void (*map_fd_put_ptr) (void *ptr);
};

struct bpf_map {
	atomic_t refcnt;
	enum bpf_map_type map_type;
	u32 key_size;
	u32 value_size;
	u32 max_entries;
	const struct bpf_map_ops *ops;
	struct work_struct work;
};

struct bpf_map_type_list {
	struct list_head list_node;
	const struct bpf_map_ops *ops;
	enum bpf_map_type type;
};

/* function argument constraints */
enum bpf_arg_type {
	ARG_DONTCARE = 0,	/* unused argument in helper function */

	/* the following constraints used to prototype
	 * bpf_map_lookup/update/delete_elem() functions
	 */
	ARG_CONST_MAP_PTR,	/* const argument used as pointer to bpf_map */
	ARG_PTR_TO_MAP_KEY,	/* pointer to stack used as map key */
	ARG_PTR_TO_MAP_VALUE,	/* pointer to stack used as map value */

	/* the following constraints used to prototype bpf_memcmp() and other
	 * functions that access data on eBPF program stack
	 */
	ARG_PTR_TO_STACK,	/* any pointer to eBPF program stack */
	ARG_CONST_STACK_SIZE,	/* number of bytes accessed from stack */

	ARG_PTR_TO_CTX,		/* pointer to context */
	ARG_ANYTHING,		/* any (initialized) argument is ok */
};

/* type of values returned from helper functions */
enum bpf_return_type {
	RET_INTEGER,			/* function returns integer */
	RET_VOID,			/* function doesn't return anything */
	RET_PTR_TO_MAP_VALUE_OR_NULL,	/* returns a pointer to map elem value or NULL */
};

/* eBPF function prototype used by verifier to allow BPF_CALLs from eBPF programs
 * to in-kernel helper functions and for adjusting imm32 field in BPF_CALL
 * instructions after verifying
 */
struct bpf_func_proto {
	u64 (*func)(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
	bool gpl_only;
	enum bpf_return_type ret_type;
	enum bpf_arg_type arg1_type;
	enum bpf_arg_type arg2_type;
	enum bpf_arg_type arg3_type;
	enum bpf_arg_type arg4_type;
	enum bpf_arg_type arg5_type;
};

/* bpf_context is intentionally undefined structure. Pointer to bpf_context is
 * the first argument to eBPF programs.
 * For socket filters: 'struct bpf_context *' == 'struct sk_buff *'
 */
struct bpf_context;

enum bpf_access_type {
	BPF_READ = 1,
	BPF_WRITE = 2
};

struct bpf_prog;

struct bpf_verifier_ops {
	/* return eBPF function prototype for verification */
	const struct bpf_func_proto *(*get_func_proto)(enum bpf_func_id func_id);

	/* return true if 'size' wide access at offset 'off' within bpf_context
	 * with 'type' (read or write) is allowed
	 */
	bool (*is_valid_access)(int off, int size, enum bpf_access_type type);

	u32 (*convert_ctx_access)(enum bpf_access_type type, int dst_reg,
				  int src_reg, int ctx_off,
				  struct bpf_insn *insn, struct bpf_prog *prog);
};

struct bpf_prog_type_list {
	struct list_head list_node;
	const struct bpf_verifier_ops *ops;
	enum bpf_prog_type type;
};

struct bpf_prog_aux {
	atomic_t refcnt;
	u32 used_map_cnt;
	const struct bpf_verifier_ops *ops;
	struct bpf_map **used_maps;
	struct bpf_prog *prog;
	union {
		struct work_struct work;
		struct rcu_head	rcu;
	};
};

struct bpf_array {
	struct bpf_map map;
	u32 elem_size;
	/* 'ownership' of prog_array is claimed by the first program that
	 * is going to use this map or by the first program which FD is stored
	 * in the map to make sure that all callers and callees have the same
	 * prog_type and JITed flag
	 */
	enum bpf_prog_type owner_prog_type;
	bool owner_jited;
	union {
		char value[0] __aligned(8);
		void *ptrs[0] __aligned(8);
	};
};
#define MAX_TAIL_CALL_CNT 32

u64 bpf_tail_call(u64 ctx, u64 r2, u64 index, u64 r4, u64 r5);
void bpf_fd_array_map_clear(struct bpf_map *map);
bool bpf_prog_array_compatible(struct bpf_array *array, const struct bpf_prog *fp);
const struct bpf_func_proto *bpf_get_trace_printk_proto(void);

#ifdef CONFIG_BPF_SYSCALL
void bpf_register_prog_type(struct bpf_prog_type_list *tl);
void bpf_register_map_type(struct bpf_map_type_list *tl);

struct bpf_prog *bpf_prog_get(u32 ufd);
void bpf_prog_put(struct bpf_prog *prog);
void bpf_prog_put_rcu(struct bpf_prog *prog);

struct bpf_map *bpf_map_get(struct fd f);
void bpf_map_put(struct bpf_map *map);

extern int sysctl_unprivileged_bpf_disabled;

/* verify correctness of eBPF program */
int bpf_check(struct bpf_prog **fp, union bpf_attr *attr);
#else
static inline void bpf_register_prog_type(struct bpf_prog_type_list *tl)
{
}

static inline struct bpf_prog *bpf_prog_get(u32 ufd)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void bpf_prog_put(struct bpf_prog *prog)
{
}
#endif /* CONFIG_BPF_SYSCALL */

/* verifier prototypes for helper functions called from eBPF programs */
extern const struct bpf_func_proto bpf_map_lookup_elem_proto;
extern const struct bpf_func_proto bpf_map_update_elem_proto;
extern const struct bpf_func_proto bpf_map_delete_elem_proto;

extern const struct bpf_func_proto bpf_perf_event_read_proto;
extern const struct bpf_func_proto bpf_get_prandom_u32_proto;
extern const struct bpf_func_proto bpf_get_smp_processor_id_proto;
extern const struct bpf_func_proto bpf_tail_call_proto;
extern const struct bpf_func_proto bpf_ktime_get_ns_proto;
extern const struct bpf_func_proto bpf_get_current_pid_tgid_proto;
extern const struct bpf_func_proto bpf_get_current_uid_gid_proto;
extern const struct bpf_func_proto bpf_get_current_comm_proto;
extern const struct bpf_func_proto bpf_skb_vlan_push_proto;
extern const struct bpf_func_proto bpf_skb_vlan_pop_proto;

/* Shared helpers among cBPF and eBPF. */
void bpf_user_rnd_init_once(void);
u64 bpf_user_rnd_u32(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);

#endif /* _LINUX_BPF_H */
