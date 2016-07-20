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
#include <linux/percpu.h>
#include <linux/err.h>

struct perf_event;
struct bpf_map;

/* map is generic key/value storage optionally accesible by eBPF programs */
struct bpf_map_ops {
	/* funcs callable from userspace (via syscall) */
	struct bpf_map *(*map_alloc)(union bpf_attr *attr);
	void (*map_release)(struct bpf_map *map, struct file *map_file);
	void (*map_free)(struct bpf_map *map);
	int (*map_get_next_key)(struct bpf_map *map, void *key, void *next_key);

	/* funcs callable from userspace and from eBPF programs */
	void *(*map_lookup_elem)(struct bpf_map *map, void *key);
	int (*map_update_elem)(struct bpf_map *map, void *key, void *value, u64 flags);
	int (*map_delete_elem)(struct bpf_map *map, void *key);

	/* funcs called by prog_array and perf_event_array map */
	void *(*map_fd_get_ptr)(struct bpf_map *map, struct file *map_file,
				int fd);
	void (*map_fd_put_ptr)(void *ptr);
};

struct bpf_map {
	atomic_t refcnt;
	enum bpf_map_type map_type;
	u32 key_size;
	u32 value_size;
	u32 max_entries;
	u32 map_flags;
	u32 pages;
	struct user_struct *user;
	const struct bpf_map_ops *ops;
	struct work_struct work;
	atomic_t usercnt;
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
	ARG_PTR_TO_RAW_STACK,	/* any pointer to eBPF program stack, area does not
				 * need to be initialized, helper function must fill
				 * all bytes or clear them in error case.
				 */

	ARG_CONST_STACK_SIZE,	/* number of bytes accessed from stack */
	ARG_CONST_STACK_SIZE_OR_ZERO, /* number of bytes accessed from stack or 0 */

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

/* types of values stored in eBPF registers */
enum bpf_reg_type {
	NOT_INIT = 0,		 /* nothing was written into register */
	UNKNOWN_VALUE,		 /* reg doesn't contain a valid pointer */
	PTR_TO_CTX,		 /* reg points to bpf_context */
	CONST_PTR_TO_MAP,	 /* reg points to struct bpf_map */
	PTR_TO_MAP_VALUE,	 /* reg points to map element value */
	PTR_TO_MAP_VALUE_OR_NULL,/* points to map elem value or NULL */
	FRAME_PTR,		 /* reg == frame_pointer */
	PTR_TO_STACK,		 /* reg == frame_pointer + imm */
	CONST_IMM,		 /* constant integer value */

	/* PTR_TO_PACKET represents:
	 * skb->data
	 * skb->data + imm
	 * skb->data + (u16) var
	 * skb->data + (u16) var + imm
	 * if (range > 0) then [ptr, ptr + range - off) is safe to access
	 * if (id > 0) means that some 'var' was added
	 * if (off > 0) menas that 'imm' was added
	 */
	PTR_TO_PACKET,
	PTR_TO_PACKET_END,	 /* skb->data + headlen */
};

struct bpf_prog;

struct bpf_verifier_ops {
	/* return eBPF function prototype for verification */
	const struct bpf_func_proto *(*get_func_proto)(enum bpf_func_id func_id);

	/* return true if 'size' wide access at offset 'off' within bpf_context
	 * with 'type' (read or write) is allowed
	 */
	bool (*is_valid_access)(int off, int size, enum bpf_access_type type,
				enum bpf_reg_type *reg_type);

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
	u32 max_ctx_offset;
	const struct bpf_verifier_ops *ops;
	struct bpf_map **used_maps;
	struct bpf_prog *prog;
	struct user_struct *user;
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
		void __percpu *pptrs[0] __aligned(8);
	};
};

#define MAX_TAIL_CALL_CNT 32

struct bpf_event_entry {
	struct perf_event *event;
	struct file *perf_file;
	struct file *map_file;
	struct rcu_head rcu;
};

u64 bpf_tail_call(u64 ctx, u64 r2, u64 index, u64 r4, u64 r5);
u64 bpf_get_stackid(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);

bool bpf_prog_array_compatible(struct bpf_array *array, const struct bpf_prog *fp);

const struct bpf_func_proto *bpf_get_trace_printk_proto(void);

typedef unsigned long (*bpf_ctx_copy_t)(void *dst, const void *src,
					unsigned long len);

u64 bpf_event_output(struct bpf_map *map, u64 flags, void *meta, u64 meta_size,
		     void *ctx, u64 ctx_size, bpf_ctx_copy_t ctx_copy);

#ifdef CONFIG_BPF_SYSCALL
DECLARE_PER_CPU(int, bpf_prog_active);

void bpf_register_prog_type(struct bpf_prog_type_list *tl);
void bpf_register_map_type(struct bpf_map_type_list *tl);

struct bpf_prog *bpf_prog_get(u32 ufd);
struct bpf_prog *bpf_prog_get_type(u32 ufd, enum bpf_prog_type type);
struct bpf_prog *bpf_prog_add(struct bpf_prog *prog, int i);
struct bpf_prog *bpf_prog_inc(struct bpf_prog *prog);
void bpf_prog_put(struct bpf_prog *prog);

struct bpf_map *bpf_map_get_with_uref(u32 ufd);
struct bpf_map *__bpf_map_get(struct fd f);
struct bpf_map *bpf_map_inc(struct bpf_map *map, bool uref);
void bpf_map_put_with_uref(struct bpf_map *map);
void bpf_map_put(struct bpf_map *map);
int bpf_map_precharge_memlock(u32 pages);

extern int sysctl_unprivileged_bpf_disabled;

int bpf_map_new_fd(struct bpf_map *map);
int bpf_prog_new_fd(struct bpf_prog *prog);

int bpf_obj_pin_user(u32 ufd, const char __user *pathname);
int bpf_obj_get_user(const char __user *pathname);

int bpf_percpu_hash_copy(struct bpf_map *map, void *key, void *value);
int bpf_percpu_array_copy(struct bpf_map *map, void *key, void *value);
int bpf_percpu_hash_update(struct bpf_map *map, void *key, void *value,
			   u64 flags);
int bpf_percpu_array_update(struct bpf_map *map, void *key, void *value,
			    u64 flags);

int bpf_stackmap_copy(struct bpf_map *map, void *key, void *value);

int bpf_fd_array_map_update_elem(struct bpf_map *map, struct file *map_file,
				 void *key, void *value, u64 map_flags);
void bpf_fd_array_map_clear(struct bpf_map *map);

/* memcpy that is used with 8-byte aligned pointers, power-of-8 size and
 * forced to use 'long' read/writes to try to atomically copy long counters.
 * Best-effort only.  No barriers here, since it _will_ race with concurrent
 * updates from BPF programs. Called from bpf syscall and mostly used with
 * size 8 or 16 bytes, so ask compiler to inline it.
 */
static inline void bpf_long_memcpy(void *dst, const void *src, u32 size)
{
	const long *lsrc = src;
	long *ldst = dst;

	size /= sizeof(long);
	while (size--)
		*ldst++ = *lsrc++;
}

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

static inline struct bpf_prog *bpf_prog_get_type(u32 ufd,
						 enum bpf_prog_type type)
{
	return ERR_PTR(-EOPNOTSUPP);
}
static inline struct bpf_prog *bpf_prog_add(struct bpf_prog *prog, int i)
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

extern const struct bpf_func_proto bpf_get_prandom_u32_proto;
extern const struct bpf_func_proto bpf_get_smp_processor_id_proto;
extern const struct bpf_func_proto bpf_tail_call_proto;
extern const struct bpf_func_proto bpf_ktime_get_ns_proto;
extern const struct bpf_func_proto bpf_get_current_pid_tgid_proto;
extern const struct bpf_func_proto bpf_get_current_uid_gid_proto;
extern const struct bpf_func_proto bpf_get_current_comm_proto;
extern const struct bpf_func_proto bpf_skb_vlan_push_proto;
extern const struct bpf_func_proto bpf_skb_vlan_pop_proto;
extern const struct bpf_func_proto bpf_get_stackid_proto;

/* Shared helpers among cBPF and eBPF. */
void bpf_user_rnd_init_once(void);
u64 bpf_user_rnd_u32(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);

#endif /* _LINUX_BPF_H */
