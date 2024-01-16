/* SPDX-License-Identifier: GPL-2.0-only */
/* Copyright (c) 2011-2014 PLUMgrid, http://plumgrid.com
 */
#ifndef _LINUX_BPF_H
#define _LINUX_BPF_H 1

#include <uapi/linux/bpf.h>
#include <uapi/linux/filter.h>

#include <linux/workqueue.h>
#include <linux/file.h>
#include <linux/percpu.h>
#include <linux/err.h>
#include <linux/rbtree_latch.h>
#include <linux/numa.h>
#include <linux/mm_types.h>
#include <linux/wait.h>
#include <linux/refcount.h>
#include <linux/mutex.h>
#include <linux/module.h>
#include <linux/kallsyms.h>
#include <linux/capability.h>
#include <linux/sched/mm.h>
#include <linux/slab.h>
#include <linux/percpu-refcount.h>
#include <linux/stddef.h>
#include <linux/bpfptr.h>
#include <linux/btf.h>
#include <linux/rcupdate_trace.h>
#include <linux/static_call.h>
#include <linux/android_kabi.h>

struct bpf_verifier_env;
struct bpf_verifier_log;
struct perf_event;
struct bpf_prog;
struct bpf_prog_aux;
struct bpf_map;
struct sock;
struct seq_file;
struct btf;
struct btf_type;
struct exception_table_entry;
struct seq_operations;
struct bpf_iter_aux_info;
struct bpf_local_storage;
struct bpf_local_storage_map;
struct kobject;
struct mem_cgroup;
struct module;
struct bpf_func_state;
struct ftrace_ops;
struct cgroup;

extern struct idr btf_idr;
extern spinlock_t btf_idr_lock;
extern struct kobject *btf_kobj;

typedef u64 (*bpf_callback_t)(u64, u64, u64, u64, u64);
typedef int (*bpf_iter_init_seq_priv_t)(void *private_data,
					struct bpf_iter_aux_info *aux);
typedef void (*bpf_iter_fini_seq_priv_t)(void *private_data);
typedef unsigned int (*bpf_func_t)(const void *,
				   const struct bpf_insn *);
struct bpf_iter_seq_info {
	const struct seq_operations *seq_ops;
	bpf_iter_init_seq_priv_t init_seq_private;
	bpf_iter_fini_seq_priv_t fini_seq_private;
	u32 seq_priv_size;
};

/* map is generic key/value storage optionally accessible by eBPF programs */
struct bpf_map_ops {
	/* funcs callable from userspace (via syscall) */
	int (*map_alloc_check)(union bpf_attr *attr);
	struct bpf_map *(*map_alloc)(union bpf_attr *attr);
	void (*map_release)(struct bpf_map *map, struct file *map_file);
	void (*map_free)(struct bpf_map *map);
	int (*map_get_next_key)(struct bpf_map *map, void *key, void *next_key);
	void (*map_release_uref)(struct bpf_map *map);
	void *(*map_lookup_elem_sys_only)(struct bpf_map *map, void *key);
	int (*map_lookup_batch)(struct bpf_map *map, const union bpf_attr *attr,
				union bpf_attr __user *uattr);
	int (*map_lookup_and_delete_elem)(struct bpf_map *map, void *key,
					  void *value, u64 flags);
	int (*map_lookup_and_delete_batch)(struct bpf_map *map,
					   const union bpf_attr *attr,
					   union bpf_attr __user *uattr);
	int (*map_update_batch)(struct bpf_map *map, const union bpf_attr *attr,
				union bpf_attr __user *uattr);
	int (*map_delete_batch)(struct bpf_map *map, const union bpf_attr *attr,
				union bpf_attr __user *uattr);

	/* funcs callable from userspace and from eBPF programs */
	void *(*map_lookup_elem)(struct bpf_map *map, void *key);
	int (*map_update_elem)(struct bpf_map *map, void *key, void *value, u64 flags);
	int (*map_delete_elem)(struct bpf_map *map, void *key);
	int (*map_push_elem)(struct bpf_map *map, void *value, u64 flags);
	int (*map_pop_elem)(struct bpf_map *map, void *value);
	int (*map_peek_elem)(struct bpf_map *map, void *value);
	void *(*map_lookup_percpu_elem)(struct bpf_map *map, void *key, u32 cpu);

	/* funcs called by prog_array and perf_event_array map */
	void *(*map_fd_get_ptr)(struct bpf_map *map, struct file *map_file,
				int fd);
	void (*map_fd_put_ptr)(void *ptr);
	int (*map_gen_lookup)(struct bpf_map *map, struct bpf_insn *insn_buf);
	u32 (*map_fd_sys_lookup_elem)(void *ptr);
	void (*map_seq_show_elem)(struct bpf_map *map, void *key,
				  struct seq_file *m);
	int (*map_check_btf)(const struct bpf_map *map,
			     const struct btf *btf,
			     const struct btf_type *key_type,
			     const struct btf_type *value_type);

	/* Prog poke tracking helpers. */
	int (*map_poke_track)(struct bpf_map *map, struct bpf_prog_aux *aux);
	void (*map_poke_untrack)(struct bpf_map *map, struct bpf_prog_aux *aux);
	void (*map_poke_run)(struct bpf_map *map, u32 key, struct bpf_prog *old,
			     struct bpf_prog *new);

	/* Direct value access helpers. */
	int (*map_direct_value_addr)(const struct bpf_map *map,
				     u64 *imm, u32 off);
	int (*map_direct_value_meta)(const struct bpf_map *map,
				     u64 imm, u32 *off);
	int (*map_mmap)(struct bpf_map *map, struct vm_area_struct *vma);
	__poll_t (*map_poll)(struct bpf_map *map, struct file *filp,
			     struct poll_table_struct *pts);

	/* Functions called by bpf_local_storage maps */
	int (*map_local_storage_charge)(struct bpf_local_storage_map *smap,
					void *owner, u32 size);
	void (*map_local_storage_uncharge)(struct bpf_local_storage_map *smap,
					   void *owner, u32 size);
	struct bpf_local_storage __rcu ** (*map_owner_storage_ptr)(void *owner);

	/* Misc helpers.*/
	int (*map_redirect)(struct bpf_map *map, u32 ifindex, u64 flags);

	/* map_meta_equal must be implemented for maps that can be
	 * used as an inner map.  It is a runtime check to ensure
	 * an inner map can be inserted to an outer map.
	 *
	 * Some properties of the inner map has been used during the
	 * verification time.  When inserting an inner map at the runtime,
	 * map_meta_equal has to ensure the inserting map has the same
	 * properties that the verifier has used earlier.
	 */
	bool (*map_meta_equal)(const struct bpf_map *meta0,
			       const struct bpf_map *meta1);


	int (*map_set_for_each_callback_args)(struct bpf_verifier_env *env,
					      struct bpf_func_state *caller,
					      struct bpf_func_state *callee);
	int (*map_for_each_callback)(struct bpf_map *map,
				     bpf_callback_t callback_fn,
				     void *callback_ctx, u64 flags);

	/* BTF id of struct allocated by map_alloc */
	int *map_btf_id;

	/* bpf_iter info used to open a seq_file */
	const struct bpf_iter_seq_info *iter_seq_info;

	ANDROID_KABI_RESERVE(1);
	ANDROID_KABI_RESERVE(2);
};

enum {
	/* Support at most 8 pointers in a BPF map value */
	BPF_MAP_VALUE_OFF_MAX = 8,
	BPF_MAP_OFF_ARR_MAX   = BPF_MAP_VALUE_OFF_MAX +
				1 + /* for bpf_spin_lock */
				1,  /* for bpf_timer */
};

enum bpf_kptr_type {
	BPF_KPTR_UNREF,
	BPF_KPTR_REF,
};

struct bpf_map_value_off_desc {
	u32 offset;
	enum bpf_kptr_type type;
	struct {
		struct btf *btf;
		struct module *module;
		btf_dtor_kfunc_t dtor;
		u32 btf_id;
	} kptr;
};

struct bpf_map_value_off {
	u32 nr_off;
	struct bpf_map_value_off_desc off[];
};

struct bpf_map_off_arr {
	u32 cnt;
	u32 field_off[BPF_MAP_OFF_ARR_MAX];
	u8 field_sz[BPF_MAP_OFF_ARR_MAX];
};

struct bpf_map {
	/* The first two cachelines with read-mostly members of which some
	 * are also accessed in fast-path (e.g. ops, max_entries).
	 */
	const struct bpf_map_ops *ops ____cacheline_aligned;
	struct bpf_map *inner_map_meta;
#ifdef CONFIG_SECURITY
	void *security;
#endif
	enum bpf_map_type map_type;
	u32 key_size;
	u32 value_size;
	u32 max_entries;
	u64 map_extra; /* any per-map-type extra fields */
	u32 map_flags;
	int spin_lock_off; /* >=0 valid offset, <0 error */
	struct bpf_map_value_off *kptr_off_tab;
	int timer_off; /* >=0 valid offset, <0 error */
	u32 id;
	int numa_node;
	u32 btf_key_type_id;
	u32 btf_value_type_id;
	u32 btf_vmlinux_value_type_id;
	struct btf *btf;
#ifdef CONFIG_MEMCG_KMEM
	struct obj_cgroup *objcg;
#endif
	char name[BPF_OBJ_NAME_LEN];
	struct bpf_map_off_arr *off_arr;
	/* The 3rd and 4th cacheline with misc members to avoid false sharing
	 * particularly with refcounting.
	 */
	atomic64_t refcnt ____cacheline_aligned;
	atomic64_t usercnt;
	struct work_struct work;
	struct mutex freeze_mutex;
	atomic64_t writecnt;
	/* 'Ownership' of program-containing map is claimed by the first program
	 * that is going to use this map or by the first program which FD is
	 * stored in the map to make sure that all callers and callees have the
	 * same prog type, JITed flag and xdp_has_frags flag.
	 */
	struct {
		spinlock_t lock;
		enum bpf_prog_type type;
		bool jited;
		bool xdp_has_frags;
	} owner;
	bool bypass_spec_v1;
	bool frozen; /* write-once; write-protected by freeze_mutex */
};

static inline bool map_value_has_spin_lock(const struct bpf_map *map)
{
	return map->spin_lock_off >= 0;
}

static inline bool map_value_has_timer(const struct bpf_map *map)
{
	return map->timer_off >= 0;
}

static inline bool map_value_has_kptrs(const struct bpf_map *map)
{
	return !IS_ERR_OR_NULL(map->kptr_off_tab);
}

/* 'dst' must be a temporary buffer and should not point to memory that is being
 * used in parallel by a bpf program or bpf syscall, otherwise the access from
 * the bpf program or bpf syscall may be corrupted by the reinitialization,
 * leading to weird problems. Even 'dst' is newly-allocated from bpf memory
 * allocator, it is still possible for 'dst' to be used in parallel by a bpf
 * program or bpf syscall.
 */
static inline void check_and_init_map_value(struct bpf_map *map, void *dst)
{
	if (unlikely(map_value_has_spin_lock(map)))
		memset(dst + map->spin_lock_off, 0, sizeof(struct bpf_spin_lock));
	if (unlikely(map_value_has_timer(map)))
		memset(dst + map->timer_off, 0, sizeof(struct bpf_timer));
	if (unlikely(map_value_has_kptrs(map))) {
		struct bpf_map_value_off *tab = map->kptr_off_tab;
		int i;

		for (i = 0; i < tab->nr_off; i++)
			*(u64 *)(dst + tab->off[i].offset) = 0;
	}
}

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
		data_race(*ldst++ = *lsrc++);
}

/* copy everything but bpf_spin_lock, bpf_timer, and kptrs. There could be one of each. */
static inline void __copy_map_value(struct bpf_map *map, void *dst, void *src, bool long_memcpy)
{
	u32 curr_off = 0;
	int i;

	if (likely(!map->off_arr)) {
		if (long_memcpy)
			bpf_long_memcpy(dst, src, round_up(map->value_size, 8));
		else
			memcpy(dst, src, map->value_size);
		return;
	}

	for (i = 0; i < map->off_arr->cnt; i++) {
		u32 next_off = map->off_arr->field_off[i];

		memcpy(dst + curr_off, src + curr_off, next_off - curr_off);
		curr_off = next_off + map->off_arr->field_sz[i];
	}
	memcpy(dst + curr_off, src + curr_off, map->value_size - curr_off);
}

static inline void copy_map_value(struct bpf_map *map, void *dst, void *src)
{
	__copy_map_value(map, dst, src, false);
}

static inline void copy_map_value_long(struct bpf_map *map, void *dst, void *src)
{
	__copy_map_value(map, dst, src, true);
}

static inline void zero_map_value(struct bpf_map *map, void *dst)
{
	u32 curr_off = 0;
	int i;

	if (likely(!map->off_arr)) {
		memset(dst, 0, map->value_size);
		return;
	}

	for (i = 0; i < map->off_arr->cnt; i++) {
		u32 next_off = map->off_arr->field_off[i];

		memset(dst + curr_off, 0, next_off - curr_off);
		curr_off = next_off + map->off_arr->field_sz[i];
	}
	memset(dst + curr_off, 0, map->value_size - curr_off);
}

void copy_map_value_locked(struct bpf_map *map, void *dst, void *src,
			   bool lock_src);
void bpf_timer_cancel_and_free(void *timer);
int bpf_obj_name_cpy(char *dst, const char *src, unsigned int size);

struct bpf_offload_dev;
struct bpf_offloaded_map;

struct bpf_map_dev_ops {
	int (*map_get_next_key)(struct bpf_offloaded_map *map,
				void *key, void *next_key);
	int (*map_lookup_elem)(struct bpf_offloaded_map *map,
			       void *key, void *value);
	int (*map_update_elem)(struct bpf_offloaded_map *map,
			       void *key, void *value, u64 flags);
	int (*map_delete_elem)(struct bpf_offloaded_map *map, void *key);

	ANDROID_KABI_RESERVE(1);
};

struct bpf_offloaded_map {
	struct bpf_map map;
	struct net_device *netdev;
	const struct bpf_map_dev_ops *dev_ops;
	void *dev_priv;
	struct list_head offloads;
};

static inline struct bpf_offloaded_map *map_to_offmap(struct bpf_map *map)
{
	return container_of(map, struct bpf_offloaded_map, map);
}

static inline bool bpf_map_offload_neutral(const struct bpf_map *map)
{
	return map->map_type == BPF_MAP_TYPE_PERF_EVENT_ARRAY;
}

static inline bool bpf_map_support_seq_show(const struct bpf_map *map)
{
	return (map->btf_value_type_id || map->btf_vmlinux_value_type_id) &&
		map->ops->map_seq_show_elem;
}

int map_check_no_btf(const struct bpf_map *map,
		     const struct btf *btf,
		     const struct btf_type *key_type,
		     const struct btf_type *value_type);

bool bpf_map_meta_equal(const struct bpf_map *meta0,
			const struct bpf_map *meta1);

extern const struct bpf_map_ops bpf_map_offload_ops;

/* bpf_type_flag contains a set of flags that are applicable to the values of
 * arg_type, ret_type and reg_type. For example, a pointer value may be null,
 * or a memory is read-only. We classify types into two categories: base types
 * and extended types. Extended types are base types combined with a type flag.
 *
 * Currently there are no more than 32 base types in arg_type, ret_type and
 * reg_types.
 */
#define BPF_BASE_TYPE_BITS	8

enum bpf_type_flag {
	/* PTR may be NULL. */
	PTR_MAYBE_NULL		= BIT(0 + BPF_BASE_TYPE_BITS),

	/* MEM is read-only. When applied on bpf_arg, it indicates the arg is
	 * compatible with both mutable and immutable memory.
	 */
	MEM_RDONLY		= BIT(1 + BPF_BASE_TYPE_BITS),

	/* MEM was "allocated" from a different helper, and cannot be mixed
	 * with regular non-MEM_ALLOC'ed MEM types.
	 */
	MEM_ALLOC		= BIT(2 + BPF_BASE_TYPE_BITS),

	/* MEM is in user address space. */
	MEM_USER		= BIT(3 + BPF_BASE_TYPE_BITS),

	/* MEM is a percpu memory. MEM_PERCPU tags PTR_TO_BTF_ID. When tagged
	 * with MEM_PERCPU, PTR_TO_BTF_ID _cannot_ be directly accessed. In
	 * order to drop this tag, it must be passed into bpf_per_cpu_ptr()
	 * or bpf_this_cpu_ptr(), which will return the pointer corresponding
	 * to the specified cpu.
	 */
	MEM_PERCPU		= BIT(4 + BPF_BASE_TYPE_BITS),

	/* Indicates that the argument will be released. */
	OBJ_RELEASE		= BIT(5 + BPF_BASE_TYPE_BITS),

	/* PTR is not trusted. This is only used with PTR_TO_BTF_ID, to mark
	 * unreferenced and referenced kptr loaded from map value using a load
	 * instruction, so that they can only be dereferenced but not escape the
	 * BPF program into the kernel (i.e. cannot be passed as arguments to
	 * kfunc or bpf helpers).
	 */
	PTR_UNTRUSTED		= BIT(6 + BPF_BASE_TYPE_BITS),

	MEM_UNINIT		= BIT(7 + BPF_BASE_TYPE_BITS),

	/* DYNPTR points to memory local to the bpf program. */
	DYNPTR_TYPE_LOCAL	= BIT(8 + BPF_BASE_TYPE_BITS),

	/* DYNPTR points to a kernel-produced ringbuf record. */
	DYNPTR_TYPE_RINGBUF	= BIT(9 + BPF_BASE_TYPE_BITS),

	/* Size is known at compile time. */
	MEM_FIXED_SIZE		= BIT(10 + BPF_BASE_TYPE_BITS),

	__BPF_TYPE_FLAG_MAX,
	__BPF_TYPE_LAST_FLAG	= __BPF_TYPE_FLAG_MAX - 1,
};

#define DYNPTR_TYPE_FLAG_MASK	(DYNPTR_TYPE_LOCAL | DYNPTR_TYPE_RINGBUF)

/* Max number of base types. */
#define BPF_BASE_TYPE_LIMIT	(1UL << BPF_BASE_TYPE_BITS)

/* Max number of all types. */
#define BPF_TYPE_LIMIT		(__BPF_TYPE_LAST_FLAG | (__BPF_TYPE_LAST_FLAG - 1))

/* function argument constraints */
enum bpf_arg_type {
	ARG_DONTCARE = 0,	/* unused argument in helper function */

	/* the following constraints used to prototype
	 * bpf_map_lookup/update/delete_elem() functions
	 */
	ARG_CONST_MAP_PTR,	/* const argument used as pointer to bpf_map */
	ARG_PTR_TO_MAP_KEY,	/* pointer to stack used as map key */
	ARG_PTR_TO_MAP_VALUE,	/* pointer to stack used as map value */

	/* Used to prototype bpf_memcmp() and other functions that access data
	 * on eBPF program stack
	 */
	ARG_PTR_TO_MEM,		/* pointer to valid memory (stack, packet, map value) */

	ARG_CONST_SIZE,		/* number of bytes accessed from memory */
	ARG_CONST_SIZE_OR_ZERO,	/* number of bytes accessed from memory or 0 */

	ARG_PTR_TO_CTX,		/* pointer to context */
	ARG_ANYTHING,		/* any (initialized) argument is ok */
	ARG_PTR_TO_SPIN_LOCK,	/* pointer to bpf_spin_lock */
	ARG_PTR_TO_SOCK_COMMON,	/* pointer to sock_common */
	ARG_PTR_TO_INT,		/* pointer to int */
	ARG_PTR_TO_LONG,	/* pointer to long */
	ARG_PTR_TO_SOCKET,	/* pointer to bpf_sock (fullsock) */
	ARG_PTR_TO_BTF_ID,	/* pointer to in-kernel struct */
	ARG_PTR_TO_ALLOC_MEM,	/* pointer to dynamically allocated memory */
	ARG_CONST_ALLOC_SIZE_OR_ZERO,	/* number of allocated bytes requested */
	ARG_PTR_TO_BTF_ID_SOCK_COMMON,	/* pointer to in-kernel sock_common or bpf-mirrored bpf_sock */
	ARG_PTR_TO_PERCPU_BTF_ID,	/* pointer to in-kernel percpu type */
	ARG_PTR_TO_FUNC,	/* pointer to a bpf program function */
	ARG_PTR_TO_STACK,	/* pointer to stack */
	ARG_PTR_TO_CONST_STR,	/* pointer to a null terminated read-only string */
	ARG_PTR_TO_TIMER,	/* pointer to bpf_timer */
	ARG_PTR_TO_KPTR,	/* pointer to referenced kptr */
	ARG_PTR_TO_DYNPTR,      /* pointer to bpf_dynptr. See bpf_type_flag for dynptr type */
	__BPF_ARG_TYPE_MAX,

	/* Extended arg_types. */
	ARG_PTR_TO_MAP_VALUE_OR_NULL	= PTR_MAYBE_NULL | ARG_PTR_TO_MAP_VALUE,
	ARG_PTR_TO_MEM_OR_NULL		= PTR_MAYBE_NULL | ARG_PTR_TO_MEM,
	ARG_PTR_TO_CTX_OR_NULL		= PTR_MAYBE_NULL | ARG_PTR_TO_CTX,
	ARG_PTR_TO_SOCKET_OR_NULL	= PTR_MAYBE_NULL | ARG_PTR_TO_SOCKET,
	ARG_PTR_TO_ALLOC_MEM_OR_NULL	= PTR_MAYBE_NULL | ARG_PTR_TO_ALLOC_MEM,
	ARG_PTR_TO_STACK_OR_NULL	= PTR_MAYBE_NULL | ARG_PTR_TO_STACK,
	ARG_PTR_TO_BTF_ID_OR_NULL	= PTR_MAYBE_NULL | ARG_PTR_TO_BTF_ID,
	/* pointer to memory does not need to be initialized, helper function must fill
	 * all bytes or clear them in error case.
	 */
	ARG_PTR_TO_UNINIT_MEM		= MEM_UNINIT | ARG_PTR_TO_MEM,
	/* Pointer to valid memory of size known at compile time. */
	ARG_PTR_TO_FIXED_SIZE_MEM	= MEM_FIXED_SIZE | ARG_PTR_TO_MEM,

	/* This must be the last entry. Its purpose is to ensure the enum is
	 * wide enough to hold the higher bits reserved for bpf_type_flag.
	 */
	__BPF_ARG_TYPE_LIMIT	= BPF_TYPE_LIMIT,
};
static_assert(__BPF_ARG_TYPE_MAX <= BPF_BASE_TYPE_LIMIT);

/* type of values returned from helper functions */
enum bpf_return_type {
	RET_INTEGER,			/* function returns integer */
	RET_VOID,			/* function doesn't return anything */
	RET_PTR_TO_MAP_VALUE,		/* returns a pointer to map elem value */
	RET_PTR_TO_SOCKET,		/* returns a pointer to a socket */
	RET_PTR_TO_TCP_SOCK,		/* returns a pointer to a tcp_sock */
	RET_PTR_TO_SOCK_COMMON,		/* returns a pointer to a sock_common */
	RET_PTR_TO_ALLOC_MEM,		/* returns a pointer to dynamically allocated memory */
	RET_PTR_TO_MEM_OR_BTF_ID,	/* returns a pointer to a valid memory or a btf_id */
	RET_PTR_TO_BTF_ID,		/* returns a pointer to a btf_id */
	__BPF_RET_TYPE_MAX,

	/* Extended ret_types. */
	RET_PTR_TO_MAP_VALUE_OR_NULL	= PTR_MAYBE_NULL | RET_PTR_TO_MAP_VALUE,
	RET_PTR_TO_SOCKET_OR_NULL	= PTR_MAYBE_NULL | RET_PTR_TO_SOCKET,
	RET_PTR_TO_TCP_SOCK_OR_NULL	= PTR_MAYBE_NULL | RET_PTR_TO_TCP_SOCK,
	RET_PTR_TO_SOCK_COMMON_OR_NULL	= PTR_MAYBE_NULL | RET_PTR_TO_SOCK_COMMON,
	RET_PTR_TO_ALLOC_MEM_OR_NULL	= PTR_MAYBE_NULL | MEM_ALLOC | RET_PTR_TO_ALLOC_MEM,
	RET_PTR_TO_DYNPTR_MEM_OR_NULL	= PTR_MAYBE_NULL | RET_PTR_TO_ALLOC_MEM,
	RET_PTR_TO_BTF_ID_OR_NULL	= PTR_MAYBE_NULL | RET_PTR_TO_BTF_ID,

	/* This must be the last entry. Its purpose is to ensure the enum is
	 * wide enough to hold the higher bits reserved for bpf_type_flag.
	 */
	__BPF_RET_TYPE_LIMIT	= BPF_TYPE_LIMIT,
};
static_assert(__BPF_RET_TYPE_MAX <= BPF_BASE_TYPE_LIMIT);

/* eBPF function prototype used by verifier to allow BPF_CALLs from eBPF programs
 * to in-kernel helper functions and for adjusting imm32 field in BPF_CALL
 * instructions after verifying
 */
struct bpf_func_proto {
	u64 (*func)(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
	bool gpl_only;
	bool pkt_access;
	enum bpf_return_type ret_type;
	union {
		struct {
			enum bpf_arg_type arg1_type;
			enum bpf_arg_type arg2_type;
			enum bpf_arg_type arg3_type;
			enum bpf_arg_type arg4_type;
			enum bpf_arg_type arg5_type;
		};
		enum bpf_arg_type arg_type[5];
	};
	union {
		struct {
			u32 *arg1_btf_id;
			u32 *arg2_btf_id;
			u32 *arg3_btf_id;
			u32 *arg4_btf_id;
			u32 *arg5_btf_id;
		};
		u32 *arg_btf_id[5];
		struct {
			size_t arg1_size;
			size_t arg2_size;
			size_t arg3_size;
			size_t arg4_size;
			size_t arg5_size;
		};
		size_t arg_size[5];
	};
	int *ret_btf_id; /* return value btf_id */
	bool (*allowed)(const struct bpf_prog *prog);
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
/* Pointer types represent:
 * pointer
 * pointer + imm
 * pointer + (u16) var
 * pointer + (u16) var + imm
 * if (range > 0) then [ptr, ptr + range - off) is safe to access
 * if (id > 0) means that some 'var' was added
 * if (off > 0) means that 'imm' was added
 */
enum bpf_reg_type {
	NOT_INIT = 0,		 /* nothing was written into register */
	SCALAR_VALUE,		 /* reg doesn't contain a valid pointer */
	PTR_TO_CTX,		 /* reg points to bpf_context */
	CONST_PTR_TO_MAP,	 /* reg points to struct bpf_map */
	PTR_TO_MAP_VALUE,	 /* reg points to map element value */
	PTR_TO_MAP_KEY,		 /* reg points to a map element key */
	PTR_TO_STACK,		 /* reg == frame_pointer + offset */
	PTR_TO_PACKET_META,	 /* skb->data - meta_len */
	PTR_TO_PACKET,		 /* reg points to skb->data */
	PTR_TO_PACKET_END,	 /* skb->data + headlen */
	PTR_TO_FLOW_KEYS,	 /* reg points to bpf_flow_keys */
	PTR_TO_SOCKET,		 /* reg points to struct bpf_sock */
	PTR_TO_SOCK_COMMON,	 /* reg points to sock_common */
	PTR_TO_TCP_SOCK,	 /* reg points to struct tcp_sock */
	PTR_TO_TP_BUFFER,	 /* reg points to a writable raw tp's buffer */
	PTR_TO_XDP_SOCK,	 /* reg points to struct xdp_sock */
	/* PTR_TO_BTF_ID points to a kernel struct that does not need
	 * to be null checked by the BPF program. This does not imply the
	 * pointer is _not_ null and in practice this can easily be a null
	 * pointer when reading pointer chains. The assumption is program
	 * context will handle null pointer dereference typically via fault
	 * handling. The verifier must keep this in mind and can make no
	 * assumptions about null or non-null when doing branch analysis.
	 * Further, when passed into helpers the helpers can not, without
	 * additional context, assume the value is non-null.
	 */
	PTR_TO_BTF_ID,
	/* PTR_TO_BTF_ID_OR_NULL points to a kernel struct that has not
	 * been checked for null. Used primarily to inform the verifier
	 * an explicit null check is required for this struct.
	 */
	PTR_TO_MEM,		 /* reg points to valid memory region */
	PTR_TO_BUF,		 /* reg points to a read/write buffer */
	PTR_TO_FUNC,		 /* reg points to a bpf program function */
	PTR_TO_DYNPTR,		 /* reg points to a dynptr */
	__BPF_REG_TYPE_MAX,

	/* Extended reg_types. */
	PTR_TO_MAP_VALUE_OR_NULL	= PTR_MAYBE_NULL | PTR_TO_MAP_VALUE,
	PTR_TO_SOCKET_OR_NULL		= PTR_MAYBE_NULL | PTR_TO_SOCKET,
	PTR_TO_SOCK_COMMON_OR_NULL	= PTR_MAYBE_NULL | PTR_TO_SOCK_COMMON,
	PTR_TO_TCP_SOCK_OR_NULL		= PTR_MAYBE_NULL | PTR_TO_TCP_SOCK,
	PTR_TO_BTF_ID_OR_NULL		= PTR_MAYBE_NULL | PTR_TO_BTF_ID,

	/* This must be the last entry. Its purpose is to ensure the enum is
	 * wide enough to hold the higher bits reserved for bpf_type_flag.
	 */
	__BPF_REG_TYPE_LIMIT	= BPF_TYPE_LIMIT,
};
static_assert(__BPF_REG_TYPE_MAX <= BPF_BASE_TYPE_LIMIT);

/* The information passed from prog-specific *_is_valid_access
 * back to the verifier.
 */
struct bpf_insn_access_aux {
	enum bpf_reg_type reg_type;
	union {
		int ctx_field_size;
		struct {
			struct btf *btf;
			u32 btf_id;
		};
	};
	struct bpf_verifier_log *log; /* for verbose logs */
};

static inline void
bpf_ctx_record_field_size(struct bpf_insn_access_aux *aux, u32 size)
{
	aux->ctx_field_size = size;
}

static inline bool bpf_pseudo_func(const struct bpf_insn *insn)
{
	return insn->code == (BPF_LD | BPF_IMM | BPF_DW) &&
	       insn->src_reg == BPF_PSEUDO_FUNC;
}

struct bpf_prog_ops {
	int (*test_run)(struct bpf_prog *prog, const union bpf_attr *kattr,
			union bpf_attr __user *uattr);
};

struct bpf_verifier_ops {
	/* return eBPF function prototype for verification */
	const struct bpf_func_proto *
	(*get_func_proto)(enum bpf_func_id func_id,
			  const struct bpf_prog *prog);

	/* return true if 'size' wide access at offset 'off' within bpf_context
	 * with 'type' (read or write) is allowed
	 */
	bool (*is_valid_access)(int off, int size, enum bpf_access_type type,
				const struct bpf_prog *prog,
				struct bpf_insn_access_aux *info);
	int (*gen_prologue)(struct bpf_insn *insn, bool direct_write,
			    const struct bpf_prog *prog);
	int (*gen_ld_abs)(const struct bpf_insn *orig,
			  struct bpf_insn *insn_buf);
	u32 (*convert_ctx_access)(enum bpf_access_type type,
				  const struct bpf_insn *src,
				  struct bpf_insn *dst,
				  struct bpf_prog *prog, u32 *target_size);
	int (*btf_struct_access)(struct bpf_verifier_log *log,
				 const struct btf *btf,
				 const struct btf_type *t, int off, int size,
				 enum bpf_access_type atype,
				 u32 *next_btf_id, enum bpf_type_flag *flag);
	ANDROID_KABI_RESERVE(1);
};

struct bpf_prog_offload_ops {
	/* verifier basic callbacks */
	int (*insn_hook)(struct bpf_verifier_env *env,
			 int insn_idx, int prev_insn_idx);
	int (*finalize)(struct bpf_verifier_env *env);
	/* verifier optimization callbacks (called after .finalize) */
	int (*replace_insn)(struct bpf_verifier_env *env, u32 off,
			    struct bpf_insn *insn);
	int (*remove_insns)(struct bpf_verifier_env *env, u32 off, u32 cnt);
	/* program management callbacks */
	int (*prepare)(struct bpf_prog *prog);
	int (*translate)(struct bpf_prog *prog);
	void (*destroy)(struct bpf_prog *prog);
	ANDROID_KABI_RESERVE(1);
};

struct bpf_prog_offload {
	struct bpf_prog		*prog;
	struct net_device	*netdev;
	struct bpf_offload_dev	*offdev;
	void			*dev_priv;
	struct list_head	offloads;
	bool			dev_state;
	bool			opt_failed;
	void			*jited_image;
	u32			jited_len;
	ANDROID_KABI_RESERVE(1);
};

enum bpf_cgroup_storage_type {
	BPF_CGROUP_STORAGE_SHARED,
	BPF_CGROUP_STORAGE_PERCPU,
	__BPF_CGROUP_STORAGE_MAX
};

#define MAX_BPF_CGROUP_STORAGE_TYPE __BPF_CGROUP_STORAGE_MAX

/* The longest tracepoint has 12 args.
 * See include/trace/bpf_probe.h
 */
#define MAX_BPF_FUNC_ARGS 12

/* The maximum number of arguments passed through registers
 * a single function may have.
 */
#define MAX_BPF_FUNC_REG_ARGS 5

/* The argument is a structure. */
#define BTF_FMODEL_STRUCT_ARG		BIT(0)

struct btf_func_model {
	u8 ret_size;
	u8 nr_args;
	u8 arg_size[MAX_BPF_FUNC_ARGS];
	u8 arg_flags[MAX_BPF_FUNC_ARGS];
};

/* Restore arguments before returning from trampoline to let original function
 * continue executing. This flag is used for fentry progs when there are no
 * fexit progs.
 */
#define BPF_TRAMP_F_RESTORE_REGS	BIT(0)
/* Call original function after fentry progs, but before fexit progs.
 * Makes sense for fentry/fexit, normal calls and indirect calls.
 */
#define BPF_TRAMP_F_CALL_ORIG		BIT(1)
/* Skip current frame and return to parent.  Makes sense for fentry/fexit
 * programs only. Should not be used with normal calls and indirect calls.
 */
#define BPF_TRAMP_F_SKIP_FRAME		BIT(2)
/* Store IP address of the caller on the trampoline stack,
 * so it's available for trampoline's programs.
 */
#define BPF_TRAMP_F_IP_ARG		BIT(3)
/* Return the return value of fentry prog. Only used by bpf_struct_ops. */
#define BPF_TRAMP_F_RET_FENTRY_RET	BIT(4)

/* Get original function from stack instead of from provided direct address.
 * Makes sense for trampolines with fexit or fmod_ret programs.
 */
#define BPF_TRAMP_F_ORIG_STACK		BIT(5)

/* This trampoline is on a function with another ftrace_ops with IPMODIFY,
 * e.g., a live patch. This flag is set and cleared by ftrace call backs,
 */
#define BPF_TRAMP_F_SHARE_IPMODIFY	BIT(6)

/* Each call __bpf_prog_enter + call bpf_func + call __bpf_prog_exit is ~50
 * bytes on x86.
 */
#define BPF_MAX_TRAMP_LINKS 38

struct bpf_tramp_links {
	struct bpf_tramp_link *links[BPF_MAX_TRAMP_LINKS];
	int nr_links;
};

struct bpf_tramp_run_ctx;

/* Different use cases for BPF trampoline:
 * 1. replace nop at the function entry (kprobe equivalent)
 *    flags = BPF_TRAMP_F_RESTORE_REGS
 *    fentry = a set of programs to run before returning from trampoline
 *
 * 2. replace nop at the function entry (kprobe + kretprobe equivalent)
 *    flags = BPF_TRAMP_F_CALL_ORIG | BPF_TRAMP_F_SKIP_FRAME
 *    orig_call = fentry_ip + MCOUNT_INSN_SIZE
 *    fentry = a set of program to run before calling original function
 *    fexit = a set of program to run after original function
 *
 * 3. replace direct call instruction anywhere in the function body
 *    or assign a function pointer for indirect call (like tcp_congestion_ops->cong_avoid)
 *    With flags = 0
 *      fentry = a set of programs to run before returning from trampoline
 *    With flags = BPF_TRAMP_F_CALL_ORIG
 *      orig_call = original callback addr or direct function addr
 *      fentry = a set of program to run before calling original function
 *      fexit = a set of program to run after original function
 */
struct bpf_tramp_image;
int arch_prepare_bpf_trampoline(struct bpf_tramp_image *tr, void *image, void *image_end,
				const struct btf_func_model *m, u32 flags,
				struct bpf_tramp_links *tlinks,
				void *orig_call);
u64 notrace __bpf_prog_enter_sleepable_recur(struct bpf_prog *prog,
					     struct bpf_tramp_run_ctx *run_ctx);
void notrace __bpf_prog_exit_sleepable_recur(struct bpf_prog *prog, u64 start,
					     struct bpf_tramp_run_ctx *run_ctx);
void notrace __bpf_tramp_enter(struct bpf_tramp_image *tr);
void notrace __bpf_tramp_exit(struct bpf_tramp_image *tr);
typedef u64 (*bpf_trampoline_enter_t)(struct bpf_prog *prog,
				      struct bpf_tramp_run_ctx *run_ctx);
typedef void (*bpf_trampoline_exit_t)(struct bpf_prog *prog, u64 start,
				      struct bpf_tramp_run_ctx *run_ctx);
bpf_trampoline_enter_t bpf_trampoline_enter(const struct bpf_prog *prog);
bpf_trampoline_exit_t bpf_trampoline_exit(const struct bpf_prog *prog);

struct bpf_ksym {
	unsigned long		 start;
	unsigned long		 end;
	char			 name[KSYM_NAME_LEN];
	struct list_head	 lnode;
	struct latch_tree_node	 tnode;
	bool			 prog;
};

enum bpf_tramp_prog_type {
	BPF_TRAMP_FENTRY,
	BPF_TRAMP_FEXIT,
	BPF_TRAMP_MODIFY_RETURN,
	BPF_TRAMP_MAX,
	BPF_TRAMP_REPLACE, /* more than MAX */
};

struct bpf_tramp_image {
	void *image;
	struct bpf_ksym ksym;
	struct percpu_ref pcref;
	void *ip_after_call;
	void *ip_epilogue;
	union {
		struct rcu_head rcu;
		struct work_struct work;
	};
};

struct bpf_trampoline {
	/* hlist for trampoline_table */
	struct hlist_node hlist;
	struct ftrace_ops *fops;
	/* serializes access to fields of this trampoline */
	struct mutex mutex;
	refcount_t refcnt;
	u32 flags;
	u64 key;
	struct {
		struct btf_func_model model;
		void *addr;
		bool ftrace_managed;
	} func;
	/* if !NULL this is BPF_PROG_TYPE_EXT program that extends another BPF
	 * program by replacing one of its functions. func.addr is the address
	 * of the function it replaced.
	 */
	struct bpf_prog *extension_prog;
	/* list of BPF programs using this trampoline */
	struct hlist_head progs_hlist[BPF_TRAMP_MAX];
	/* Number of attached programs. A counter per kind. */
	int progs_cnt[BPF_TRAMP_MAX];
	/* Executable image of trampoline */
	struct bpf_tramp_image *cur_image;
	u64 selector;
	struct module *mod;
	ANDROID_KABI_RESERVE(1);
};

struct bpf_attach_target_info {
	struct btf_func_model fmodel;
	long tgt_addr;
	const char *tgt_name;
	const struct btf_type *tgt_type;
};

#define BPF_DISPATCHER_MAX 48 /* Fits in 2048B */

struct bpf_dispatcher_prog {
	struct bpf_prog *prog;
	refcount_t users;
};

struct bpf_dispatcher {
	/* dispatcher mutex */
	struct mutex mutex;
	void *func;
	struct bpf_dispatcher_prog progs[BPF_DISPATCHER_MAX];
	int num_progs;
	void *image;
	void *rw_image;
	u32 image_off;
	struct bpf_ksym ksym;
#ifdef CONFIG_HAVE_STATIC_CALL
	struct static_call_key *sc_key;
	void *sc_tramp;
#endif
	ANDROID_KABI_RESERVE(1);
};

static __always_inline __nocfi unsigned int bpf_dispatcher_nop_func(
	const void *ctx,
	const struct bpf_insn *insnsi,
	bpf_func_t bpf_func)
{
	return bpf_func(ctx, insnsi);
}

#ifdef CONFIG_BPF_JIT
int bpf_trampoline_link_prog(struct bpf_tramp_link *link, struct bpf_trampoline *tr);
int bpf_trampoline_unlink_prog(struct bpf_tramp_link *link, struct bpf_trampoline *tr);
struct bpf_trampoline *bpf_trampoline_get(u64 key,
					  struct bpf_attach_target_info *tgt_info);
void bpf_trampoline_put(struct bpf_trampoline *tr);
int arch_prepare_bpf_dispatcher(void *image, void *buf, s64 *funcs, int num_funcs);

/*
 * When the architecture supports STATIC_CALL replace the bpf_dispatcher_fn
 * indirection with a direct call to the bpf program. If the architecture does
 * not have STATIC_CALL, avoid a double-indirection.
 */
#ifdef CONFIG_HAVE_STATIC_CALL

#define __BPF_DISPATCHER_SC_INIT(_name)				\
	.sc_key = &STATIC_CALL_KEY(_name),			\
	.sc_tramp = STATIC_CALL_TRAMP_ADDR(_name),

#define __BPF_DISPATCHER_SC(name)				\
	DEFINE_STATIC_CALL(bpf_dispatcher_##name##_call, bpf_dispatcher_nop_func)

#define __BPF_DISPATCHER_CALL(name)				\
	static_call(bpf_dispatcher_##name##_call)(ctx, insnsi, bpf_func)

#define __BPF_DISPATCHER_UPDATE(_d, _new)			\
	__static_call_update((_d)->sc_key, (_d)->sc_tramp, (_new))

#else
#define __BPF_DISPATCHER_SC_INIT(name)
#define __BPF_DISPATCHER_SC(name)
#define __BPF_DISPATCHER_CALL(name)		bpf_func(ctx, insnsi)
#define __BPF_DISPATCHER_UPDATE(_d, _new)
#endif

#define BPF_DISPATCHER_INIT(_name) {				\
	.mutex = __MUTEX_INITIALIZER(_name.mutex),		\
	.func = &_name##_func,					\
	.progs = {},						\
	.num_progs = 0,						\
	.image = NULL,						\
	.image_off = 0,						\
	.ksym = {						\
		.name  = #_name,				\
		.lnode = LIST_HEAD_INIT(_name.ksym.lnode),	\
	},							\
	__BPF_DISPATCHER_SC_INIT(_name##_call)			\
}

#define DEFINE_BPF_DISPATCHER(name)					\
	__BPF_DISPATCHER_SC(name);					\
	noinline __nocfi unsigned int bpf_dispatcher_##name##_func(	\
		const void *ctx,					\
		const struct bpf_insn *insnsi,				\
		bpf_func_t bpf_func)					\
	{								\
		return __BPF_DISPATCHER_CALL(name);			\
	}								\
	EXPORT_SYMBOL(bpf_dispatcher_##name##_func);			\
	struct bpf_dispatcher bpf_dispatcher_##name =			\
		BPF_DISPATCHER_INIT(bpf_dispatcher_##name);

#define DECLARE_BPF_DISPATCHER(name)					\
	unsigned int bpf_dispatcher_##name##_func(			\
		const void *ctx,					\
		const struct bpf_insn *insnsi,				\
		bpf_func_t bpf_func);					\
	extern struct bpf_dispatcher bpf_dispatcher_##name;

#define BPF_DISPATCHER_FUNC(name) bpf_dispatcher_##name##_func
#define BPF_DISPATCHER_PTR(name) (&bpf_dispatcher_##name)
void bpf_dispatcher_change_prog(struct bpf_dispatcher *d, struct bpf_prog *from,
				struct bpf_prog *to);
/* Called only from JIT-enabled code, so there's no need for stubs. */
void bpf_image_ksym_add(void *data, struct bpf_ksym *ksym);
void bpf_image_ksym_del(struct bpf_ksym *ksym);
void bpf_ksym_add(struct bpf_ksym *ksym);
void bpf_ksym_del(struct bpf_ksym *ksym);
int bpf_jit_charge_modmem(u32 size);
void bpf_jit_uncharge_modmem(u32 size);
bool bpf_prog_has_trampoline(const struct bpf_prog *prog);
#else
static inline int bpf_trampoline_link_prog(struct bpf_tramp_link *link,
					   struct bpf_trampoline *tr)
{
	return -ENOTSUPP;
}
static inline int bpf_trampoline_unlink_prog(struct bpf_tramp_link *link,
					     struct bpf_trampoline *tr)
{
	return -ENOTSUPP;
}
static inline struct bpf_trampoline *bpf_trampoline_get(u64 key,
							struct bpf_attach_target_info *tgt_info)
{
	return NULL;
}
static inline void bpf_trampoline_put(struct bpf_trampoline *tr) {}
#define DEFINE_BPF_DISPATCHER(name)
#define DECLARE_BPF_DISPATCHER(name)
#define BPF_DISPATCHER_FUNC(name) bpf_dispatcher_nop_func
#define BPF_DISPATCHER_PTR(name) NULL
static inline void bpf_dispatcher_change_prog(struct bpf_dispatcher *d,
					      struct bpf_prog *from,
					      struct bpf_prog *to) {}
static inline bool is_bpf_image_address(unsigned long address)
{
	return false;
}
static inline bool bpf_prog_has_trampoline(const struct bpf_prog *prog)
{
	return false;
}
#endif

struct bpf_func_info_aux {
	u16 linkage;
	bool unreliable;
};

enum bpf_jit_poke_reason {
	BPF_POKE_REASON_TAIL_CALL,
};

/* Descriptor of pokes pointing /into/ the JITed image. */
struct bpf_jit_poke_descriptor {
	void *tailcall_target;
	void *tailcall_bypass;
	void *bypass_addr;
	void *aux;
	union {
		struct {
			struct bpf_map *map;
			u32 key;
		} tail_call;
	};
	bool tailcall_target_stable;
	u8 adj_off;
	u16 reason;
	u32 insn_idx;
};

/* reg_type info for ctx arguments */
struct bpf_ctx_arg_aux {
	u32 offset;
	enum bpf_reg_type reg_type;
	u32 btf_id;
};

struct btf_mod_pair {
	struct btf *btf;
	struct module *module;
};

struct bpf_kfunc_desc_tab;

struct bpf_prog_aux {
	atomic64_t refcnt;
	u32 used_map_cnt;
	u32 used_btf_cnt;
	u32 max_ctx_offset;
	u32 max_pkt_offset;
	u32 max_tp_access;
	u32 stack_depth;
	u32 id;
	u32 func_cnt; /* used by non-func prog as the number of func progs */
	u32 func_idx; /* 0 for non-func prog, the index in func array for func prog */
	u32 attach_btf_id; /* in-kernel BTF type id to attach to */
	u32 ctx_arg_info_size;
	u32 max_rdonly_access;
	u32 max_rdwr_access;
	struct btf *attach_btf;
	const struct bpf_ctx_arg_aux *ctx_arg_info;
	struct mutex dst_mutex; /* protects dst_* pointers below, *after* prog becomes visible */
	struct bpf_prog *dst_prog;
	struct bpf_trampoline *dst_trampoline;
	enum bpf_prog_type saved_dst_prog_type;
	enum bpf_attach_type saved_dst_attach_type;
	bool verifier_zext; /* Zero extensions has been inserted by verifier. */
	bool offload_requested;
	bool attach_btf_trace; /* true if attaching to BTF-enabled raw tp */
	bool func_proto_unreliable;
	bool sleepable;
	bool tail_call_reachable;
	bool xdp_has_frags;
	/* BTF_KIND_FUNC_PROTO for valid attach_btf_id */
	const struct btf_type *attach_func_proto;
	/* function name for valid attach_btf_id */
	const char *attach_func_name;
	struct bpf_prog **func;
	void *jit_data; /* JIT specific data. arch dependent */
	struct bpf_jit_poke_descriptor *poke_tab;
	struct bpf_kfunc_desc_tab *kfunc_tab;
	struct bpf_kfunc_btf_tab *kfunc_btf_tab;
	u32 size_poke_tab;
	struct bpf_ksym ksym;
	const struct bpf_prog_ops *ops;
	struct bpf_map **used_maps;
	struct mutex used_maps_mutex; /* mutex for used_maps and used_map_cnt */
	struct btf_mod_pair *used_btfs;
	struct bpf_prog *prog;
	struct user_struct *user;
	u64 load_time; /* ns since boottime */
	u32 verified_insns;
	int cgroup_atype; /* enum cgroup_bpf_attach_type */
	struct bpf_map *cgroup_storage[MAX_BPF_CGROUP_STORAGE_TYPE];
	char name[BPF_OBJ_NAME_LEN];
#ifdef CONFIG_SECURITY
	void *security;
#endif
	struct bpf_prog_offload *offload;
	struct btf *btf;
	struct bpf_func_info *func_info;
	struct bpf_func_info_aux *func_info_aux;
	/* bpf_line_info loaded from userspace.  linfo->insn_off
	 * has the xlated insn offset.
	 * Both the main and sub prog share the same linfo.
	 * The subprog can access its first linfo by
	 * using the linfo_idx.
	 */
	struct bpf_line_info *linfo;
	/* jited_linfo is the jited addr of the linfo.  It has a
	 * one to one mapping to linfo:
	 * jited_linfo[i] is the jited addr for the linfo[i]->insn_off.
	 * Both the main and sub prog share the same jited_linfo.
	 * The subprog can access its first jited_linfo by
	 * using the linfo_idx.
	 */
	void **jited_linfo;
	u32 func_info_cnt;
	u32 nr_linfo;
	/* subprog can use linfo_idx to access its first linfo and
	 * jited_linfo.
	 * main prog always has linfo_idx == 0
	 */
	u32 linfo_idx;
	u32 num_exentries;
	struct exception_table_entry *extable;
	union {
		struct work_struct work;
		struct rcu_head	rcu;
	};
	ANDROID_KABI_RESERVE(1);
};

struct bpf_prog {
	u16			pages;		/* Number of allocated pages */
	u16			jited:1,	/* Is our filter JIT'ed? */
				jit_requested:1,/* archs need to JIT the prog */
				gpl_compatible:1, /* Is filter GPL compatible? */
				cb_access:1,	/* Is control block accessed? */
				dst_needed:1,	/* Do we need dst entry? */
				blinding_requested:1, /* needs constant blinding */
				blinded:1,	/* Was blinded */
				is_func:1,	/* program is a bpf function */
				kprobe_override:1, /* Do we override a kprobe? */
				has_callchain_buf:1, /* callchain buffer allocated? */
				enforce_expected_attach_type:1, /* Enforce expected_attach_type checking at attach time */
				call_get_stack:1, /* Do we call bpf_get_stack() or bpf_get_stackid() */
				call_get_func_ip:1, /* Do we call get_func_ip() */
				tstamp_type_access:1; /* Accessed __sk_buff->tstamp_type */
	enum bpf_prog_type	type;		/* Type of BPF program */
	enum bpf_attach_type	expected_attach_type; /* For some prog types */
	u32			len;		/* Number of filter blocks */
	u32			jited_len;	/* Size of jited insns in bytes */
	u8			tag[BPF_TAG_SIZE];
	struct bpf_prog_stats __percpu *stats;
	int __percpu		*active;
	unsigned int		(*bpf_func)(const void *ctx,
					    const struct bpf_insn *insn);
	struct bpf_prog_aux	*aux;		/* Auxiliary fields */
	struct sock_fprog_kern	*orig_prog;	/* Original BPF program */
	ANDROID_KABI_RESERVE(1);
	/* Instructions for interpreter */
	union {
		DECLARE_FLEX_ARRAY(struct sock_filter, insns);
		DECLARE_FLEX_ARRAY(struct bpf_insn, insnsi);
	};
};

struct bpf_array_aux {
	/* Programs with direct jumps into programs part of this array. */
	struct list_head poke_progs;
	struct bpf_map *map;
	struct mutex poke_mutex;
	struct work_struct work;
};

struct bpf_link {
	atomic64_t refcnt;
	u32 id;
	enum bpf_link_type type;
	const struct bpf_link_ops *ops;
	struct bpf_prog *prog;
	struct work_struct work;
};

struct bpf_link_ops {
	void (*release)(struct bpf_link *link);
	void (*dealloc)(struct bpf_link *link);
	int (*detach)(struct bpf_link *link);
	int (*update_prog)(struct bpf_link *link, struct bpf_prog *new_prog,
			   struct bpf_prog *old_prog);
	void (*show_fdinfo)(const struct bpf_link *link, struct seq_file *seq);
	int (*fill_link_info)(const struct bpf_link *link,
			      struct bpf_link_info *info);
	ANDROID_KABI_RESERVE(1);
};

struct bpf_tramp_link {
	struct bpf_link link;
	struct hlist_node tramp_hlist;
	u64 cookie;
};

struct bpf_shim_tramp_link {
	struct bpf_tramp_link link;
	struct bpf_trampoline *trampoline;
};

struct bpf_tracing_link {
	struct bpf_tramp_link link;
	enum bpf_attach_type attach_type;
	struct bpf_trampoline *trampoline;
	struct bpf_prog *tgt_prog;
};

struct bpf_link_primer {
	struct bpf_link *link;
	struct file *file;
	int fd;
	u32 id;
};

struct bpf_struct_ops_value;
struct btf_member;

#define BPF_STRUCT_OPS_MAX_NR_MEMBERS 64
struct bpf_struct_ops {
	const struct bpf_verifier_ops *verifier_ops;
	int (*init)(struct btf *btf);
	int (*check_member)(const struct btf_type *t,
			    const struct btf_member *member);
	int (*init_member)(const struct btf_type *t,
			   const struct btf_member *member,
			   void *kdata, const void *udata);
	int (*reg)(void *kdata);
	void (*unreg)(void *kdata);
	const struct btf_type *type;
	const struct btf_type *value_type;
	const char *name;
	struct btf_func_model func_models[BPF_STRUCT_OPS_MAX_NR_MEMBERS];
	u32 type_id;
	u32 value_id;
	ANDROID_KABI_RESERVE(1);
};

#if defined(CONFIG_BPF_JIT) && defined(CONFIG_BPF_SYSCALL)
#define BPF_MODULE_OWNER ((void *)((0xeB9FUL << 2) + POISON_POINTER_DELTA))
const struct bpf_struct_ops *bpf_struct_ops_find(u32 type_id);
void bpf_struct_ops_init(struct btf *btf, struct bpf_verifier_log *log);
bool bpf_struct_ops_get(const void *kdata);
void bpf_struct_ops_put(const void *kdata);
int bpf_struct_ops_map_sys_lookup_elem(struct bpf_map *map, void *key,
				       void *value);
int bpf_struct_ops_prepare_trampoline(struct bpf_tramp_links *tlinks,
				      struct bpf_tramp_link *link,
				      const struct btf_func_model *model,
				      void *image, void *image_end);
static inline bool bpf_try_module_get(const void *data, struct module *owner)
{
	if (owner == BPF_MODULE_OWNER)
		return bpf_struct_ops_get(data);
	else
		return try_module_get(owner);
}
static inline void bpf_module_put(const void *data, struct module *owner)
{
	if (owner == BPF_MODULE_OWNER)
		bpf_struct_ops_put(data);
	else
		module_put(owner);
}

#ifdef CONFIG_NET
/* Define it here to avoid the use of forward declaration */
struct bpf_dummy_ops_state {
	int val;
};

struct bpf_dummy_ops {
	int (*test_1)(struct bpf_dummy_ops_state *cb);
	int (*test_2)(struct bpf_dummy_ops_state *cb, int a1, unsigned short a2,
		      char a3, unsigned long a4);
};

int bpf_struct_ops_test_run(struct bpf_prog *prog, const union bpf_attr *kattr,
			    union bpf_attr __user *uattr);
#endif
#else
static inline const struct bpf_struct_ops *bpf_struct_ops_find(u32 type_id)
{
	return NULL;
}
static inline void bpf_struct_ops_init(struct btf *btf,
				       struct bpf_verifier_log *log)
{
}
static inline bool bpf_try_module_get(const void *data, struct module *owner)
{
	return try_module_get(owner);
}
static inline void bpf_module_put(const void *data, struct module *owner)
{
	module_put(owner);
}
static inline int bpf_struct_ops_map_sys_lookup_elem(struct bpf_map *map,
						     void *key,
						     void *value)
{
	return -EINVAL;
}
#endif

#if defined(CONFIG_CGROUP_BPF) && defined(CONFIG_BPF_LSM)
int bpf_trampoline_link_cgroup_shim(struct bpf_prog *prog,
				    int cgroup_atype);
void bpf_trampoline_unlink_cgroup_shim(struct bpf_prog *prog);
#else
static inline int bpf_trampoline_link_cgroup_shim(struct bpf_prog *prog,
						  int cgroup_atype)
{
	return -EOPNOTSUPP;
}
static inline void bpf_trampoline_unlink_cgroup_shim(struct bpf_prog *prog)
{
}
#endif

struct bpf_array {
	struct bpf_map map;
	u32 elem_size;
	u32 index_mask;
	struct bpf_array_aux *aux;
	union {
		char value[0] __aligned(8);
		void *ptrs[0] __aligned(8);
		void __percpu *pptrs[0] __aligned(8);
	};
};

#define BPF_COMPLEXITY_LIMIT_INSNS      1000000 /* yes. 1M insns */
#define MAX_TAIL_CALL_CNT 33

/* Maximum number of loops for bpf_loop */
#define BPF_MAX_LOOPS	BIT(23)

#define BPF_F_ACCESS_MASK	(BPF_F_RDONLY |		\
				 BPF_F_RDONLY_PROG |	\
				 BPF_F_WRONLY |		\
				 BPF_F_WRONLY_PROG)

#define BPF_MAP_CAN_READ	BIT(0)
#define BPF_MAP_CAN_WRITE	BIT(1)

/* Maximum number of user-producer ring buffer samples that can be drained in
 * a call to bpf_user_ringbuf_drain().
 */
#define BPF_MAX_USER_RINGBUF_SAMPLES (128 * 1024)

static inline u32 bpf_map_flags_to_cap(struct bpf_map *map)
{
	u32 access_flags = map->map_flags & (BPF_F_RDONLY_PROG | BPF_F_WRONLY_PROG);

	/* Combination of BPF_F_RDONLY_PROG | BPF_F_WRONLY_PROG is
	 * not possible.
	 */
	if (access_flags & BPF_F_RDONLY_PROG)
		return BPF_MAP_CAN_READ;
	else if (access_flags & BPF_F_WRONLY_PROG)
		return BPF_MAP_CAN_WRITE;
	else
		return BPF_MAP_CAN_READ | BPF_MAP_CAN_WRITE;
}

static inline bool bpf_map_flags_access_ok(u32 access_flags)
{
	return (access_flags & (BPF_F_RDONLY_PROG | BPF_F_WRONLY_PROG)) !=
	       (BPF_F_RDONLY_PROG | BPF_F_WRONLY_PROG);
}

struct bpf_event_entry {
	struct perf_event *event;
	struct file *perf_file;
	struct file *map_file;
	struct rcu_head rcu;
};

static inline bool map_type_contains_progs(struct bpf_map *map)
{
	return map->map_type == BPF_MAP_TYPE_PROG_ARRAY ||
	       map->map_type == BPF_MAP_TYPE_DEVMAP ||
	       map->map_type == BPF_MAP_TYPE_CPUMAP;
}

bool bpf_prog_map_compatible(struct bpf_map *map, const struct bpf_prog *fp);
int bpf_prog_calc_tag(struct bpf_prog *fp);

const struct bpf_func_proto *bpf_get_trace_printk_proto(void);
const struct bpf_func_proto *bpf_get_trace_vprintk_proto(void);

typedef unsigned long (*bpf_ctx_copy_t)(void *dst, const void *src,
					unsigned long off, unsigned long len);
typedef u32 (*bpf_convert_ctx_access_t)(enum bpf_access_type type,
					const struct bpf_insn *src,
					struct bpf_insn *dst,
					struct bpf_prog *prog,
					u32 *target_size);

u64 bpf_event_output(struct bpf_map *map, u64 flags, void *meta, u64 meta_size,
		     void *ctx, u64 ctx_size, bpf_ctx_copy_t ctx_copy);

/* an array of programs to be executed under rcu_lock.
 *
 * Typical usage:
 * ret = bpf_prog_run_array(rcu_dereference(&bpf_prog_array), ctx, bpf_prog_run);
 *
 * the structure returned by bpf_prog_array_alloc() should be populated
 * with program pointers and the last pointer must be NULL.
 * The user has to keep refcnt on the program and make sure the program
 * is removed from the array before bpf_prog_put().
 * The 'struct bpf_prog_array *' should only be replaced with xchg()
 * since other cpus are walking the array of pointers in parallel.
 */
struct bpf_prog_array_item {
	struct bpf_prog *prog;
	union {
		struct bpf_cgroup_storage *cgroup_storage[MAX_BPF_CGROUP_STORAGE_TYPE];
		u64 bpf_cookie;
	};
};

struct bpf_prog_array {
	struct rcu_head rcu;
	struct bpf_prog_array_item items[];
};

struct bpf_empty_prog_array {
	struct bpf_prog_array hdr;
	struct bpf_prog *null_prog;
};

/* to avoid allocating empty bpf_prog_array for cgroups that
 * don't have bpf program attached use one global 'bpf_empty_prog_array'
 * It will not be modified the caller of bpf_prog_array_alloc()
 * (since caller requested prog_cnt == 0)
 * that pointer should be 'freed' by bpf_prog_array_free()
 */
extern struct bpf_empty_prog_array bpf_empty_prog_array;

struct bpf_prog_array *bpf_prog_array_alloc(u32 prog_cnt, gfp_t flags);
void bpf_prog_array_free(struct bpf_prog_array *progs);
/* Use when traversal over the bpf_prog_array uses tasks_trace rcu */
void bpf_prog_array_free_sleepable(struct bpf_prog_array *progs);
int bpf_prog_array_length(struct bpf_prog_array *progs);
bool bpf_prog_array_is_empty(struct bpf_prog_array *array);
int bpf_prog_array_copy_to_user(struct bpf_prog_array *progs,
				__u32 __user *prog_ids, u32 cnt);

void bpf_prog_array_delete_safe(struct bpf_prog_array *progs,
				struct bpf_prog *old_prog);
int bpf_prog_array_delete_safe_at(struct bpf_prog_array *array, int index);
int bpf_prog_array_update_at(struct bpf_prog_array *array, int index,
			     struct bpf_prog *prog);
int bpf_prog_array_copy_info(struct bpf_prog_array *array,
			     u32 *prog_ids, u32 request_cnt,
			     u32 *prog_cnt);
int bpf_prog_array_copy(struct bpf_prog_array *old_array,
			struct bpf_prog *exclude_prog,
			struct bpf_prog *include_prog,
			u64 bpf_cookie,
			struct bpf_prog_array **new_array);

struct bpf_run_ctx {};

struct bpf_cg_run_ctx {
	struct bpf_run_ctx run_ctx;
	const struct bpf_prog_array_item *prog_item;
	int retval;
};

struct bpf_trace_run_ctx {
	struct bpf_run_ctx run_ctx;
	u64 bpf_cookie;
};

struct bpf_tramp_run_ctx {
	struct bpf_run_ctx run_ctx;
	u64 bpf_cookie;
	struct bpf_run_ctx *saved_run_ctx;
};

static inline struct bpf_run_ctx *bpf_set_run_ctx(struct bpf_run_ctx *new_ctx)
{
	struct bpf_run_ctx *old_ctx = NULL;

#ifdef CONFIG_BPF_SYSCALL
	old_ctx = current->bpf_ctx;
	current->bpf_ctx = new_ctx;
#endif
	return old_ctx;
}

static inline void bpf_reset_run_ctx(struct bpf_run_ctx *old_ctx)
{
#ifdef CONFIG_BPF_SYSCALL
	current->bpf_ctx = old_ctx;
#endif
}

/* BPF program asks to bypass CAP_NET_BIND_SERVICE in bind. */
#define BPF_RET_BIND_NO_CAP_NET_BIND_SERVICE			(1 << 0)
/* BPF program asks to set CN on the packet. */
#define BPF_RET_SET_CN						(1 << 0)

typedef u32 (*bpf_prog_run_fn)(const struct bpf_prog *prog, const void *ctx);

static __always_inline u32
bpf_prog_run_array(const struct bpf_prog_array *array,
		   const void *ctx, bpf_prog_run_fn run_prog)
{
	const struct bpf_prog_array_item *item;
	const struct bpf_prog *prog;
	struct bpf_run_ctx *old_run_ctx;
	struct bpf_trace_run_ctx run_ctx;
	u32 ret = 1;

	RCU_LOCKDEP_WARN(!rcu_read_lock_held(), "no rcu lock held");

	if (unlikely(!array))
		return ret;

	migrate_disable();
	old_run_ctx = bpf_set_run_ctx(&run_ctx.run_ctx);
	item = &array->items[0];
	while ((prog = READ_ONCE(item->prog))) {
		run_ctx.bpf_cookie = item->bpf_cookie;
		ret &= run_prog(prog, ctx);
		item++;
	}
	bpf_reset_run_ctx(old_run_ctx);
	migrate_enable();
	return ret;
}

/* Notes on RCU design for bpf_prog_arrays containing sleepable programs:
 *
 * We use the tasks_trace rcu flavor read section to protect the bpf_prog_array
 * overall. As a result, we must use the bpf_prog_array_free_sleepable
 * in order to use the tasks_trace rcu grace period.
 *
 * When a non-sleepable program is inside the array, we take the rcu read
 * section and disable preemption for that program alone, so it can access
 * rcu-protected dynamically sized maps.
 */
static __always_inline u32
bpf_prog_run_array_sleepable(const struct bpf_prog_array __rcu *array_rcu,
			     const void *ctx, bpf_prog_run_fn run_prog)
{
	const struct bpf_prog_array_item *item;
	const struct bpf_prog *prog;
	const struct bpf_prog_array *array;
	struct bpf_run_ctx *old_run_ctx;
	struct bpf_trace_run_ctx run_ctx;
	u32 ret = 1;

	might_fault();

	rcu_read_lock_trace();
	migrate_disable();

	array = rcu_dereference_check(array_rcu, rcu_read_lock_trace_held());
	if (unlikely(!array))
		goto out;
	old_run_ctx = bpf_set_run_ctx(&run_ctx.run_ctx);
	item = &array->items[0];
	while ((prog = READ_ONCE(item->prog))) {
		if (!prog->aux->sleepable)
			rcu_read_lock();

		run_ctx.bpf_cookie = item->bpf_cookie;
		ret &= run_prog(prog, ctx);
		item++;

		if (!prog->aux->sleepable)
			rcu_read_unlock();
	}
	bpf_reset_run_ctx(old_run_ctx);
out:
	migrate_enable();
	rcu_read_unlock_trace();
	return ret;
}

#ifdef CONFIG_BPF_SYSCALL
DECLARE_PER_CPU(int, bpf_prog_active);
extern struct mutex bpf_stats_enabled_mutex;

/*
 * Block execution of BPF programs attached to instrumentation (perf,
 * kprobes, tracepoints) to prevent deadlocks on map operations as any of
 * these events can happen inside a region which holds a map bucket lock
 * and can deadlock on it.
 */
static inline void bpf_disable_instrumentation(void)
{
	migrate_disable();
	this_cpu_inc(bpf_prog_active);
}

static inline void bpf_enable_instrumentation(void)
{
	this_cpu_dec(bpf_prog_active);
	migrate_enable();
}

extern const struct file_operations bpf_map_fops;
extern const struct file_operations bpf_prog_fops;
extern const struct file_operations bpf_iter_fops;

#define BPF_PROG_TYPE(_id, _name, prog_ctx_type, kern_ctx_type) \
	extern const struct bpf_prog_ops _name ## _prog_ops; \
	extern const struct bpf_verifier_ops _name ## _verifier_ops;
#define BPF_MAP_TYPE(_id, _ops) \
	extern const struct bpf_map_ops _ops;
#define BPF_LINK_TYPE(_id, _name)
#include <linux/bpf_types.h>
#undef BPF_PROG_TYPE
#undef BPF_MAP_TYPE
#undef BPF_LINK_TYPE

extern const struct bpf_prog_ops bpf_offload_prog_ops;
extern const struct bpf_verifier_ops tc_cls_act_analyzer_ops;
extern const struct bpf_verifier_ops xdp_analyzer_ops;

struct bpf_prog *bpf_prog_get(u32 ufd);
struct bpf_prog *bpf_prog_get_type_dev(u32 ufd, enum bpf_prog_type type,
				       bool attach_drv);
void bpf_prog_add(struct bpf_prog *prog, int i);
void bpf_prog_sub(struct bpf_prog *prog, int i);
void bpf_prog_inc(struct bpf_prog *prog);
struct bpf_prog * __must_check bpf_prog_inc_not_zero(struct bpf_prog *prog);
void bpf_prog_put(struct bpf_prog *prog);

void bpf_prog_free_id(struct bpf_prog *prog, bool do_idr_lock);
void bpf_map_free_id(struct bpf_map *map, bool do_idr_lock);

struct bpf_map_value_off_desc *bpf_map_kptr_off_contains(struct bpf_map *map, u32 offset);
void bpf_map_free_kptr_off_tab(struct bpf_map *map);
struct bpf_map_value_off *bpf_map_copy_kptr_off_tab(const struct bpf_map *map);
bool bpf_map_equal_kptr_off_tab(const struct bpf_map *map_a, const struct bpf_map *map_b);
void bpf_map_free_kptrs(struct bpf_map *map, void *map_value);

struct bpf_map *bpf_map_get(u32 ufd);
struct bpf_map *bpf_map_get_with_uref(u32 ufd);
struct bpf_map *__bpf_map_get(struct fd f);
void bpf_map_inc(struct bpf_map *map);
void bpf_map_inc_with_uref(struct bpf_map *map);
struct bpf_map * __must_check bpf_map_inc_not_zero(struct bpf_map *map);
void bpf_map_put_with_uref(struct bpf_map *map);
void bpf_map_put(struct bpf_map *map);
void *bpf_map_area_alloc(u64 size, int numa_node);
void *bpf_map_area_mmapable_alloc(u64 size, int numa_node);
void bpf_map_area_free(void *base);
bool bpf_map_write_active(const struct bpf_map *map);
void bpf_map_init_from_attr(struct bpf_map *map, union bpf_attr *attr);
int  generic_map_lookup_batch(struct bpf_map *map,
			      const union bpf_attr *attr,
			      union bpf_attr __user *uattr);
int  generic_map_update_batch(struct bpf_map *map,
			      const union bpf_attr *attr,
			      union bpf_attr __user *uattr);
int  generic_map_delete_batch(struct bpf_map *map,
			      const union bpf_attr *attr,
			      union bpf_attr __user *uattr);
struct bpf_map *bpf_map_get_curr_or_next(u32 *id);
struct bpf_prog *bpf_prog_get_curr_or_next(u32 *id);

#ifdef CONFIG_MEMCG_KMEM
void *bpf_map_kmalloc_node(const struct bpf_map *map, size_t size, gfp_t flags,
			   int node);
void *bpf_map_kzalloc(const struct bpf_map *map, size_t size, gfp_t flags);
void __percpu *bpf_map_alloc_percpu(const struct bpf_map *map, size_t size,
				    size_t align, gfp_t flags);
#else
static inline void *
bpf_map_kmalloc_node(const struct bpf_map *map, size_t size, gfp_t flags,
		     int node)
{
	return kmalloc_node(size, flags, node);
}

static inline void *
bpf_map_kzalloc(const struct bpf_map *map, size_t size, gfp_t flags)
{
	return kzalloc(size, flags);
}

static inline void __percpu *
bpf_map_alloc_percpu(const struct bpf_map *map, size_t size, size_t align,
		     gfp_t flags)
{
	return __alloc_percpu_gfp(size, align, flags);
}
#endif

extern int sysctl_unprivileged_bpf_disabled;

static inline bool bpf_allow_ptr_leaks(void)
{
	return perfmon_capable();
}

static inline bool bpf_allow_uninit_stack(void)
{
	return perfmon_capable();
}

static inline bool bpf_allow_ptr_to_map_access(void)
{
	return perfmon_capable();
}

static inline bool bpf_bypass_spec_v1(void)
{
	return perfmon_capable();
}

static inline bool bpf_bypass_spec_v4(void)
{
	return perfmon_capable();
}

int bpf_map_new_fd(struct bpf_map *map, int flags);
int bpf_prog_new_fd(struct bpf_prog *prog);

void bpf_link_init(struct bpf_link *link, enum bpf_link_type type,
		   const struct bpf_link_ops *ops, struct bpf_prog *prog);
int bpf_link_prime(struct bpf_link *link, struct bpf_link_primer *primer);
int bpf_link_settle(struct bpf_link_primer *primer);
void bpf_link_cleanup(struct bpf_link_primer *primer);
void bpf_link_inc(struct bpf_link *link);
void bpf_link_put(struct bpf_link *link);
int bpf_link_new_fd(struct bpf_link *link);
struct file *bpf_link_new_file(struct bpf_link *link, int *reserved_fd);
struct bpf_link *bpf_link_get_from_fd(u32 ufd);
struct bpf_link *bpf_link_get_curr_or_next(u32 *id);

int bpf_obj_pin_user(u32 ufd, const char __user *pathname);
int bpf_obj_get_user(const char __user *pathname, int flags);

#define BPF_ITER_FUNC_PREFIX "bpf_iter_"
#define DEFINE_BPF_ITER_FUNC(target, args...)			\
	extern int bpf_iter_ ## target(args);			\
	int __init bpf_iter_ ## target(args) { return 0; }

/*
 * The task type of iterators.
 *
 * For BPF task iterators, they can be parameterized with various
 * parameters to visit only some of tasks.
 *
 * BPF_TASK_ITER_ALL (default)
 *	Iterate over resources of every task.
 *
 * BPF_TASK_ITER_TID
 *	Iterate over resources of a task/tid.
 *
 * BPF_TASK_ITER_TGID
 *	Iterate over resources of every task of a process / task group.
 */
enum bpf_iter_task_type {
	BPF_TASK_ITER_ALL = 0,
	BPF_TASK_ITER_TID,
	BPF_TASK_ITER_TGID,
};

struct bpf_iter_aux_info {
	/* for map_elem iter */
	struct bpf_map *map;

	/* for cgroup iter */
	struct {
		struct cgroup *start; /* starting cgroup */
		enum bpf_cgroup_iter_order order;
	} cgroup;
	struct {
		enum bpf_iter_task_type	type;
		u32 pid;
	} task;
};

typedef int (*bpf_iter_attach_target_t)(struct bpf_prog *prog,
					union bpf_iter_link_info *linfo,
					struct bpf_iter_aux_info *aux);
typedef void (*bpf_iter_detach_target_t)(struct bpf_iter_aux_info *aux);
typedef void (*bpf_iter_show_fdinfo_t) (const struct bpf_iter_aux_info *aux,
					struct seq_file *seq);
typedef int (*bpf_iter_fill_link_info_t)(const struct bpf_iter_aux_info *aux,
					 struct bpf_link_info *info);
typedef const struct bpf_func_proto *
(*bpf_iter_get_func_proto_t)(enum bpf_func_id func_id,
			     const struct bpf_prog *prog);

enum bpf_iter_feature {
	BPF_ITER_RESCHED	= BIT(0),
};

#define BPF_ITER_CTX_ARG_MAX 2
struct bpf_iter_reg {
	const char *target;
	bpf_iter_attach_target_t attach_target;
	bpf_iter_detach_target_t detach_target;
	bpf_iter_show_fdinfo_t show_fdinfo;
	bpf_iter_fill_link_info_t fill_link_info;
	bpf_iter_get_func_proto_t get_func_proto;
	u32 ctx_arg_info_size;
	u32 feature;
	struct bpf_ctx_arg_aux ctx_arg_info[BPF_ITER_CTX_ARG_MAX];
	const struct bpf_iter_seq_info *seq_info;
};

struct bpf_iter_meta {
	__bpf_md_ptr(struct seq_file *, seq);
	u64 session_id;
	u64 seq_num;
};

struct bpf_iter__bpf_map_elem {
	__bpf_md_ptr(struct bpf_iter_meta *, meta);
	__bpf_md_ptr(struct bpf_map *, map);
	__bpf_md_ptr(void *, key);
	__bpf_md_ptr(void *, value);
};

int bpf_iter_reg_target(const struct bpf_iter_reg *reg_info);
void bpf_iter_unreg_target(const struct bpf_iter_reg *reg_info);
bool bpf_iter_prog_supported(struct bpf_prog *prog);
const struct bpf_func_proto *
bpf_iter_get_func_proto(enum bpf_func_id func_id, const struct bpf_prog *prog);
int bpf_iter_link_attach(const union bpf_attr *attr, bpfptr_t uattr, struct bpf_prog *prog);
int bpf_iter_new_fd(struct bpf_link *link);
bool bpf_link_is_iter(struct bpf_link *link);
struct bpf_prog *bpf_iter_get_info(struct bpf_iter_meta *meta, bool in_stop);
int bpf_iter_run_prog(struct bpf_prog *prog, void *ctx);
void bpf_iter_map_show_fdinfo(const struct bpf_iter_aux_info *aux,
			      struct seq_file *seq);
int bpf_iter_map_fill_link_info(const struct bpf_iter_aux_info *aux,
				struct bpf_link_info *info);

int map_set_for_each_callback_args(struct bpf_verifier_env *env,
				   struct bpf_func_state *caller,
				   struct bpf_func_state *callee);

int bpf_percpu_hash_copy(struct bpf_map *map, void *key, void *value);
int bpf_percpu_array_copy(struct bpf_map *map, void *key, void *value);
int bpf_percpu_hash_update(struct bpf_map *map, void *key, void *value,
			   u64 flags);
int bpf_percpu_array_update(struct bpf_map *map, void *key, void *value,
			    u64 flags);

int bpf_stackmap_copy(struct bpf_map *map, void *key, void *value);

int bpf_fd_array_map_update_elem(struct bpf_map *map, struct file *map_file,
				 void *key, void *value, u64 map_flags);
int bpf_fd_array_map_lookup_elem(struct bpf_map *map, void *key, u32 *value);
int bpf_fd_htab_map_update_elem(struct bpf_map *map, struct file *map_file,
				void *key, void *value, u64 map_flags);
int bpf_fd_htab_map_lookup_elem(struct bpf_map *map, void *key, u32 *value);

int bpf_get_file_flag(int flags);
int bpf_check_uarg_tail_zero(bpfptr_t uaddr, size_t expected_size,
			     size_t actual_size);

/* verify correctness of eBPF program */
int bpf_check(struct bpf_prog **fp, union bpf_attr *attr, bpfptr_t uattr);

#ifndef CONFIG_BPF_JIT_ALWAYS_ON
void bpf_patch_call_args(struct bpf_insn *insn, u32 stack_depth);
#endif

struct btf *bpf_get_btf_vmlinux(void);

/* Map specifics */
struct xdp_frame;
struct sk_buff;
struct bpf_dtab_netdev;
struct bpf_cpu_map_entry;

void __dev_flush(void);
int dev_xdp_enqueue(struct net_device *dev, struct xdp_frame *xdpf,
		    struct net_device *dev_rx);
int dev_map_enqueue(struct bpf_dtab_netdev *dst, struct xdp_frame *xdpf,
		    struct net_device *dev_rx);
int dev_map_enqueue_multi(struct xdp_frame *xdpf, struct net_device *dev_rx,
			  struct bpf_map *map, bool exclude_ingress);
int dev_map_generic_redirect(struct bpf_dtab_netdev *dst, struct sk_buff *skb,
			     struct bpf_prog *xdp_prog);
int dev_map_redirect_multi(struct net_device *dev, struct sk_buff *skb,
			   struct bpf_prog *xdp_prog, struct bpf_map *map,
			   bool exclude_ingress);

void __cpu_map_flush(void);
int cpu_map_enqueue(struct bpf_cpu_map_entry *rcpu, struct xdp_frame *xdpf,
		    struct net_device *dev_rx);
int cpu_map_generic_redirect(struct bpf_cpu_map_entry *rcpu,
			     struct sk_buff *skb);

/* Return map's numa specified by userspace */
static inline int bpf_map_attr_numa_node(const union bpf_attr *attr)
{
	return (attr->map_flags & BPF_F_NUMA_NODE) ?
		attr->numa_node : NUMA_NO_NODE;
}

struct bpf_prog *bpf_prog_get_type_path(const char *name, enum bpf_prog_type type);
int array_map_alloc_check(union bpf_attr *attr);

int bpf_prog_test_run_xdp(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr);
int bpf_prog_test_run_skb(struct bpf_prog *prog, const union bpf_attr *kattr,
			  union bpf_attr __user *uattr);
int bpf_prog_test_run_tracing(struct bpf_prog *prog,
			      const union bpf_attr *kattr,
			      union bpf_attr __user *uattr);
int bpf_prog_test_run_flow_dissector(struct bpf_prog *prog,
				     const union bpf_attr *kattr,
				     union bpf_attr __user *uattr);
int bpf_prog_test_run_raw_tp(struct bpf_prog *prog,
			     const union bpf_attr *kattr,
			     union bpf_attr __user *uattr);
int bpf_prog_test_run_sk_lookup(struct bpf_prog *prog,
				const union bpf_attr *kattr,
				union bpf_attr __user *uattr);
bool btf_ctx_access(int off, int size, enum bpf_access_type type,
		    const struct bpf_prog *prog,
		    struct bpf_insn_access_aux *info);

static inline bool bpf_tracing_ctx_access(int off, int size,
					  enum bpf_access_type type)
{
	if (off < 0 || off >= sizeof(__u64) * MAX_BPF_FUNC_ARGS)
		return false;
	if (type != BPF_READ)
		return false;
	if (off % size != 0)
		return false;
	return true;
}

static inline bool bpf_tracing_btf_ctx_access(int off, int size,
					      enum bpf_access_type type,
					      const struct bpf_prog *prog,
					      struct bpf_insn_access_aux *info)
{
	if (!bpf_tracing_ctx_access(off, size, type))
		return false;
	return btf_ctx_access(off, size, type, prog, info);
}

int btf_struct_access(struct bpf_verifier_log *log, const struct btf *btf,
		      const struct btf_type *t, int off, int size,
		      enum bpf_access_type atype,
		      u32 *next_btf_id, enum bpf_type_flag *flag);
bool btf_struct_ids_match(struct bpf_verifier_log *log,
			  const struct btf *btf, u32 id, int off,
			  const struct btf *need_btf, u32 need_type_id,
			  bool strict);

int btf_distill_func_proto(struct bpf_verifier_log *log,
			   struct btf *btf,
			   const struct btf_type *func_proto,
			   const char *func_name,
			   struct btf_func_model *m);

struct bpf_kfunc_arg_meta {
	u64 r0_size;
	bool r0_rdonly;
	int ref_obj_id;
	u32 flags;
};

struct bpf_reg_state;
int btf_check_subprog_arg_match(struct bpf_verifier_env *env, int subprog,
				struct bpf_reg_state *regs);
int btf_check_subprog_call(struct bpf_verifier_env *env, int subprog,
			   struct bpf_reg_state *regs);
int btf_check_kfunc_arg_match(struct bpf_verifier_env *env,
			      const struct btf *btf, u32 func_id,
			      struct bpf_reg_state *regs,
			      struct bpf_kfunc_arg_meta *meta);
int btf_prepare_func_args(struct bpf_verifier_env *env, int subprog,
			  struct bpf_reg_state *reg);
int btf_check_type_match(struct bpf_verifier_log *log, const struct bpf_prog *prog,
			 struct btf *btf, const struct btf_type *t);

struct bpf_prog *bpf_prog_by_id(u32 id);
struct bpf_link *bpf_link_by_id(u32 id);

const struct bpf_func_proto *bpf_base_func_proto(enum bpf_func_id func_id);
void bpf_task_storage_free(struct task_struct *task);
bool bpf_prog_has_kfunc_call(const struct bpf_prog *prog);
const struct btf_func_model *
bpf_jit_find_kfunc_model(const struct bpf_prog *prog,
			 const struct bpf_insn *insn);
struct bpf_core_ctx {
	struct bpf_verifier_log *log;
	const struct btf *btf;
};

int bpf_core_apply(struct bpf_core_ctx *ctx, const struct bpf_core_relo *relo,
		   int relo_idx, void *insn);

static inline bool unprivileged_ebpf_enabled(void)
{
	return !sysctl_unprivileged_bpf_disabled;
}

/* Not all bpf prog type has the bpf_ctx.
 * For the bpf prog type that has initialized the bpf_ctx,
 * this function can be used to decide if a kernel function
 * is called by a bpf program.
 */
static inline bool has_current_bpf_ctx(void)
{
	return !!current->bpf_ctx;
}

void notrace bpf_prog_inc_misses_counter(struct bpf_prog *prog);
#else /* !CONFIG_BPF_SYSCALL */
static inline struct bpf_prog *bpf_prog_get(u32 ufd)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline struct bpf_prog *bpf_prog_get_type_dev(u32 ufd,
						     enum bpf_prog_type type,
						     bool attach_drv)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void bpf_prog_add(struct bpf_prog *prog, int i)
{
}

static inline void bpf_prog_sub(struct bpf_prog *prog, int i)
{
}

static inline void bpf_prog_put(struct bpf_prog *prog)
{
}

static inline void bpf_prog_inc(struct bpf_prog *prog)
{
}

static inline struct bpf_prog *__must_check
bpf_prog_inc_not_zero(struct bpf_prog *prog)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void bpf_link_init(struct bpf_link *link, enum bpf_link_type type,
				 const struct bpf_link_ops *ops,
				 struct bpf_prog *prog)
{
}

static inline int bpf_link_prime(struct bpf_link *link,
				 struct bpf_link_primer *primer)
{
	return -EOPNOTSUPP;
}

static inline int bpf_link_settle(struct bpf_link_primer *primer)
{
	return -EOPNOTSUPP;
}

static inline void bpf_link_cleanup(struct bpf_link_primer *primer)
{
}

static inline void bpf_link_inc(struct bpf_link *link)
{
}

static inline void bpf_link_put(struct bpf_link *link)
{
}

static inline int bpf_obj_get_user(const char __user *pathname, int flags)
{
	return -EOPNOTSUPP;
}

static inline void __dev_flush(void)
{
}

struct xdp_frame;
struct bpf_dtab_netdev;
struct bpf_cpu_map_entry;

static inline
int dev_xdp_enqueue(struct net_device *dev, struct xdp_frame *xdpf,
		    struct net_device *dev_rx)
{
	return 0;
}

static inline
int dev_map_enqueue(struct bpf_dtab_netdev *dst, struct xdp_frame *xdpf,
		    struct net_device *dev_rx)
{
	return 0;
}

static inline
int dev_map_enqueue_multi(struct xdp_frame *xdpf, struct net_device *dev_rx,
			  struct bpf_map *map, bool exclude_ingress)
{
	return 0;
}

struct sk_buff;

static inline int dev_map_generic_redirect(struct bpf_dtab_netdev *dst,
					   struct sk_buff *skb,
					   struct bpf_prog *xdp_prog)
{
	return 0;
}

static inline
int dev_map_redirect_multi(struct net_device *dev, struct sk_buff *skb,
			   struct bpf_prog *xdp_prog, struct bpf_map *map,
			   bool exclude_ingress)
{
	return 0;
}

static inline void __cpu_map_flush(void)
{
}

static inline int cpu_map_enqueue(struct bpf_cpu_map_entry *rcpu,
				  struct xdp_frame *xdpf,
				  struct net_device *dev_rx)
{
	return 0;
}

static inline int cpu_map_generic_redirect(struct bpf_cpu_map_entry *rcpu,
					   struct sk_buff *skb)
{
	return -EOPNOTSUPP;
}

static inline struct bpf_prog *bpf_prog_get_type_path(const char *name,
				enum bpf_prog_type type)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int bpf_prog_test_run_xdp(struct bpf_prog *prog,
					const union bpf_attr *kattr,
					union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

static inline int bpf_prog_test_run_skb(struct bpf_prog *prog,
					const union bpf_attr *kattr,
					union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

static inline int bpf_prog_test_run_tracing(struct bpf_prog *prog,
					    const union bpf_attr *kattr,
					    union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

static inline int bpf_prog_test_run_flow_dissector(struct bpf_prog *prog,
						   const union bpf_attr *kattr,
						   union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

static inline int bpf_prog_test_run_sk_lookup(struct bpf_prog *prog,
					      const union bpf_attr *kattr,
					      union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

static inline void bpf_map_put(struct bpf_map *map)
{
}

static inline struct bpf_prog *bpf_prog_by_id(u32 id)
{
	return ERR_PTR(-ENOTSUPP);
}

static inline int btf_struct_access(struct bpf_verifier_log *log,
				    const struct btf *btf,
				    const struct btf_type *t, int off, int size,
				    enum bpf_access_type atype,
				    u32 *next_btf_id, enum bpf_type_flag *flag)
{
	return -EACCES;
}

static inline const struct bpf_func_proto *
bpf_base_func_proto(enum bpf_func_id func_id)
{
	return NULL;
}

static inline void bpf_task_storage_free(struct task_struct *task)
{
}

static inline bool bpf_prog_has_kfunc_call(const struct bpf_prog *prog)
{
	return false;
}

static inline const struct btf_func_model *
bpf_jit_find_kfunc_model(const struct bpf_prog *prog,
			 const struct bpf_insn *insn)
{
	return NULL;
}

static inline bool unprivileged_ebpf_enabled(void)
{
	return false;
}

static inline bool has_current_bpf_ctx(void)
{
	return false;
}

static inline void bpf_prog_inc_misses_counter(struct bpf_prog *prog)
{
}
#endif /* CONFIG_BPF_SYSCALL */

void __bpf_free_used_btfs(struct bpf_prog_aux *aux,
			  struct btf_mod_pair *used_btfs, u32 len);

static inline struct bpf_prog *bpf_prog_get_type(u32 ufd,
						 enum bpf_prog_type type)
{
	return bpf_prog_get_type_dev(ufd, type, false);
}

void __bpf_free_used_maps(struct bpf_prog_aux *aux,
			  struct bpf_map **used_maps, u32 len);

bool bpf_prog_get_ok(struct bpf_prog *, enum bpf_prog_type *, bool);

int bpf_prog_offload_compile(struct bpf_prog *prog);
void bpf_prog_offload_destroy(struct bpf_prog *prog);
int bpf_prog_offload_info_fill(struct bpf_prog_info *info,
			       struct bpf_prog *prog);

int bpf_map_offload_info_fill(struct bpf_map_info *info, struct bpf_map *map);

int bpf_map_offload_lookup_elem(struct bpf_map *map, void *key, void *value);
int bpf_map_offload_update_elem(struct bpf_map *map,
				void *key, void *value, u64 flags);
int bpf_map_offload_delete_elem(struct bpf_map *map, void *key);
int bpf_map_offload_get_next_key(struct bpf_map *map,
				 void *key, void *next_key);

bool bpf_offload_prog_map_match(struct bpf_prog *prog, struct bpf_map *map);

struct bpf_offload_dev *
bpf_offload_dev_create(const struct bpf_prog_offload_ops *ops, void *priv);
void bpf_offload_dev_destroy(struct bpf_offload_dev *offdev);
void *bpf_offload_dev_priv(struct bpf_offload_dev *offdev);
int bpf_offload_dev_netdev_register(struct bpf_offload_dev *offdev,
				    struct net_device *netdev);
void bpf_offload_dev_netdev_unregister(struct bpf_offload_dev *offdev,
				       struct net_device *netdev);
bool bpf_offload_dev_match(struct bpf_prog *prog, struct net_device *netdev);

void unpriv_ebpf_notify(int new_state);

#if defined(CONFIG_NET) && defined(CONFIG_BPF_SYSCALL)
int bpf_prog_offload_init(struct bpf_prog *prog, union bpf_attr *attr);

static inline bool bpf_prog_is_dev_bound(const struct bpf_prog_aux *aux)
{
	return aux->offload_requested;
}

static inline bool bpf_map_is_dev_bound(struct bpf_map *map)
{
	return unlikely(map->ops == &bpf_map_offload_ops);
}

struct bpf_map *bpf_map_offload_map_alloc(union bpf_attr *attr);
void bpf_map_offload_map_free(struct bpf_map *map);
int bpf_prog_test_run_syscall(struct bpf_prog *prog,
			      const union bpf_attr *kattr,
			      union bpf_attr __user *uattr);

int sock_map_get_from_fd(const union bpf_attr *attr, struct bpf_prog *prog);
int sock_map_prog_detach(const union bpf_attr *attr, enum bpf_prog_type ptype);
int sock_map_update_elem_sys(struct bpf_map *map, void *key, void *value, u64 flags);
int sock_map_bpf_prog_query(const union bpf_attr *attr,
			    union bpf_attr __user *uattr);

void sock_map_unhash(struct sock *sk);
void sock_map_destroy(struct sock *sk);
void sock_map_close(struct sock *sk, long timeout);
#else
static inline int bpf_prog_offload_init(struct bpf_prog *prog,
					union bpf_attr *attr)
{
	return -EOPNOTSUPP;
}

static inline bool bpf_prog_is_dev_bound(struct bpf_prog_aux *aux)
{
	return false;
}

static inline bool bpf_map_is_dev_bound(struct bpf_map *map)
{
	return false;
}

static inline struct bpf_map *bpf_map_offload_map_alloc(union bpf_attr *attr)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline void bpf_map_offload_map_free(struct bpf_map *map)
{
}

static inline int bpf_prog_test_run_syscall(struct bpf_prog *prog,
					    const union bpf_attr *kattr,
					    union bpf_attr __user *uattr)
{
	return -ENOTSUPP;
}

#ifdef CONFIG_BPF_SYSCALL
static inline int sock_map_get_from_fd(const union bpf_attr *attr,
				       struct bpf_prog *prog)
{
	return -EINVAL;
}

static inline int sock_map_prog_detach(const union bpf_attr *attr,
				       enum bpf_prog_type ptype)
{
	return -EOPNOTSUPP;
}

static inline int sock_map_update_elem_sys(struct bpf_map *map, void *key, void *value,
					   u64 flags)
{
	return -EOPNOTSUPP;
}

static inline int sock_map_bpf_prog_query(const union bpf_attr *attr,
					  union bpf_attr __user *uattr)
{
	return -EINVAL;
}
#endif /* CONFIG_BPF_SYSCALL */
#endif /* CONFIG_NET && CONFIG_BPF_SYSCALL */

#if defined(CONFIG_INET) && defined(CONFIG_BPF_SYSCALL)
void bpf_sk_reuseport_detach(struct sock *sk);
int bpf_fd_reuseport_array_lookup_elem(struct bpf_map *map, void *key,
				       void *value);
int bpf_fd_reuseport_array_update_elem(struct bpf_map *map, void *key,
				       void *value, u64 map_flags);
#else
static inline void bpf_sk_reuseport_detach(struct sock *sk)
{
}

#ifdef CONFIG_BPF_SYSCALL
static inline int bpf_fd_reuseport_array_lookup_elem(struct bpf_map *map,
						     void *key, void *value)
{
	return -EOPNOTSUPP;
}

static inline int bpf_fd_reuseport_array_update_elem(struct bpf_map *map,
						     void *key, void *value,
						     u64 map_flags)
{
	return -EOPNOTSUPP;
}
#endif /* CONFIG_BPF_SYSCALL */
#endif /* defined(CONFIG_INET) && defined(CONFIG_BPF_SYSCALL) */

/* verifier prototypes for helper functions called from eBPF programs */
extern const struct bpf_func_proto bpf_map_lookup_elem_proto;
extern const struct bpf_func_proto bpf_map_update_elem_proto;
extern const struct bpf_func_proto bpf_map_delete_elem_proto;
extern const struct bpf_func_proto bpf_map_push_elem_proto;
extern const struct bpf_func_proto bpf_map_pop_elem_proto;
extern const struct bpf_func_proto bpf_map_peek_elem_proto;
extern const struct bpf_func_proto bpf_map_lookup_percpu_elem_proto;

extern const struct bpf_func_proto bpf_get_prandom_u32_proto;
extern const struct bpf_func_proto bpf_get_smp_processor_id_proto;
extern const struct bpf_func_proto bpf_get_numa_node_id_proto;
extern const struct bpf_func_proto bpf_tail_call_proto;
extern const struct bpf_func_proto bpf_ktime_get_ns_proto;
extern const struct bpf_func_proto bpf_ktime_get_boot_ns_proto;
extern const struct bpf_func_proto bpf_ktime_get_tai_ns_proto;
extern const struct bpf_func_proto bpf_get_current_pid_tgid_proto;
extern const struct bpf_func_proto bpf_get_current_uid_gid_proto;
extern const struct bpf_func_proto bpf_get_current_comm_proto;
extern const struct bpf_func_proto bpf_get_stackid_proto;
extern const struct bpf_func_proto bpf_get_stack_proto;
extern const struct bpf_func_proto bpf_get_task_stack_proto;
extern const struct bpf_func_proto bpf_get_stackid_proto_pe;
extern const struct bpf_func_proto bpf_get_stack_proto_pe;
extern const struct bpf_func_proto bpf_sock_map_update_proto;
extern const struct bpf_func_proto bpf_sock_hash_update_proto;
extern const struct bpf_func_proto bpf_get_current_cgroup_id_proto;
extern const struct bpf_func_proto bpf_get_current_ancestor_cgroup_id_proto;
extern const struct bpf_func_proto bpf_get_cgroup_classid_curr_proto;
extern const struct bpf_func_proto bpf_msg_redirect_hash_proto;
extern const struct bpf_func_proto bpf_msg_redirect_map_proto;
extern const struct bpf_func_proto bpf_sk_redirect_hash_proto;
extern const struct bpf_func_proto bpf_sk_redirect_map_proto;
extern const struct bpf_func_proto bpf_spin_lock_proto;
extern const struct bpf_func_proto bpf_spin_unlock_proto;
extern const struct bpf_func_proto bpf_get_local_storage_proto;
extern const struct bpf_func_proto bpf_strtol_proto;
extern const struct bpf_func_proto bpf_strtoul_proto;
extern const struct bpf_func_proto bpf_tcp_sock_proto;
extern const struct bpf_func_proto bpf_jiffies64_proto;
extern const struct bpf_func_proto bpf_get_ns_current_pid_tgid_proto;
extern const struct bpf_func_proto bpf_event_output_data_proto;
extern const struct bpf_func_proto bpf_ringbuf_output_proto;
extern const struct bpf_func_proto bpf_ringbuf_reserve_proto;
extern const struct bpf_func_proto bpf_ringbuf_submit_proto;
extern const struct bpf_func_proto bpf_ringbuf_discard_proto;
extern const struct bpf_func_proto bpf_ringbuf_query_proto;
extern const struct bpf_func_proto bpf_ringbuf_reserve_dynptr_proto;
extern const struct bpf_func_proto bpf_ringbuf_submit_dynptr_proto;
extern const struct bpf_func_proto bpf_ringbuf_discard_dynptr_proto;
extern const struct bpf_func_proto bpf_skc_to_tcp6_sock_proto;
extern const struct bpf_func_proto bpf_skc_to_tcp_sock_proto;
extern const struct bpf_func_proto bpf_skc_to_tcp_timewait_sock_proto;
extern const struct bpf_func_proto bpf_skc_to_tcp_request_sock_proto;
extern const struct bpf_func_proto bpf_skc_to_udp6_sock_proto;
extern const struct bpf_func_proto bpf_skc_to_unix_sock_proto;
extern const struct bpf_func_proto bpf_skc_to_mptcp_sock_proto;
extern const struct bpf_func_proto bpf_copy_from_user_proto;
extern const struct bpf_func_proto bpf_snprintf_btf_proto;
extern const struct bpf_func_proto bpf_snprintf_proto;
extern const struct bpf_func_proto bpf_per_cpu_ptr_proto;
extern const struct bpf_func_proto bpf_this_cpu_ptr_proto;
extern const struct bpf_func_proto bpf_ktime_get_coarse_ns_proto;
extern const struct bpf_func_proto bpf_sock_from_file_proto;
extern const struct bpf_func_proto bpf_get_socket_ptr_cookie_proto;
extern const struct bpf_func_proto bpf_task_storage_get_proto;
extern const struct bpf_func_proto bpf_task_storage_delete_proto;
extern const struct bpf_func_proto bpf_for_each_map_elem_proto;
extern const struct bpf_func_proto bpf_btf_find_by_name_kind_proto;
extern const struct bpf_func_proto bpf_sk_setsockopt_proto;
extern const struct bpf_func_proto bpf_sk_getsockopt_proto;
extern const struct bpf_func_proto bpf_unlocked_sk_setsockopt_proto;
extern const struct bpf_func_proto bpf_unlocked_sk_getsockopt_proto;
extern const struct bpf_func_proto bpf_find_vma_proto;
extern const struct bpf_func_proto bpf_loop_proto;
extern const struct bpf_func_proto bpf_copy_from_user_task_proto;
extern const struct bpf_func_proto bpf_set_retval_proto;
extern const struct bpf_func_proto bpf_get_retval_proto;
extern const struct bpf_func_proto bpf_user_ringbuf_drain_proto;

const struct bpf_func_proto *tracing_prog_func_proto(
  enum bpf_func_id func_id, const struct bpf_prog *prog);

/* Shared helpers among cBPF and eBPF. */
void bpf_user_rnd_init_once(void);
u64 bpf_user_rnd_u32(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);
u64 bpf_get_raw_cpu_id(u64 r1, u64 r2, u64 r3, u64 r4, u64 r5);

#if defined(CONFIG_NET)
bool bpf_sock_common_is_valid_access(int off, int size,
				     enum bpf_access_type type,
				     struct bpf_insn_access_aux *info);
bool bpf_sock_is_valid_access(int off, int size, enum bpf_access_type type,
			      struct bpf_insn_access_aux *info);
u32 bpf_sock_convert_ctx_access(enum bpf_access_type type,
				const struct bpf_insn *si,
				struct bpf_insn *insn_buf,
				struct bpf_prog *prog,
				u32 *target_size);
#else
static inline bool bpf_sock_common_is_valid_access(int off, int size,
						   enum bpf_access_type type,
						   struct bpf_insn_access_aux *info)
{
	return false;
}
static inline bool bpf_sock_is_valid_access(int off, int size,
					    enum bpf_access_type type,
					    struct bpf_insn_access_aux *info)
{
	return false;
}
static inline u32 bpf_sock_convert_ctx_access(enum bpf_access_type type,
					      const struct bpf_insn *si,
					      struct bpf_insn *insn_buf,
					      struct bpf_prog *prog,
					      u32 *target_size)
{
	return 0;
}
#endif

#ifdef CONFIG_INET
struct sk_reuseport_kern {
	struct sk_buff *skb;
	struct sock *sk;
	struct sock *selected_sk;
	struct sock *migrating_sk;
	void *data_end;
	u32 hash;
	u32 reuseport_id;
	bool bind_inany;
};
bool bpf_tcp_sock_is_valid_access(int off, int size, enum bpf_access_type type,
				  struct bpf_insn_access_aux *info);

u32 bpf_tcp_sock_convert_ctx_access(enum bpf_access_type type,
				    const struct bpf_insn *si,
				    struct bpf_insn *insn_buf,
				    struct bpf_prog *prog,
				    u32 *target_size);

bool bpf_xdp_sock_is_valid_access(int off, int size, enum bpf_access_type type,
				  struct bpf_insn_access_aux *info);

u32 bpf_xdp_sock_convert_ctx_access(enum bpf_access_type type,
				    const struct bpf_insn *si,
				    struct bpf_insn *insn_buf,
				    struct bpf_prog *prog,
				    u32 *target_size);
#else
static inline bool bpf_tcp_sock_is_valid_access(int off, int size,
						enum bpf_access_type type,
						struct bpf_insn_access_aux *info)
{
	return false;
}

static inline u32 bpf_tcp_sock_convert_ctx_access(enum bpf_access_type type,
						  const struct bpf_insn *si,
						  struct bpf_insn *insn_buf,
						  struct bpf_prog *prog,
						  u32 *target_size)
{
	return 0;
}
static inline bool bpf_xdp_sock_is_valid_access(int off, int size,
						enum bpf_access_type type,
						struct bpf_insn_access_aux *info)
{
	return false;
}

static inline u32 bpf_xdp_sock_convert_ctx_access(enum bpf_access_type type,
						  const struct bpf_insn *si,
						  struct bpf_insn *insn_buf,
						  struct bpf_prog *prog,
						  u32 *target_size)
{
	return 0;
}
#endif /* CONFIG_INET */

enum bpf_text_poke_type {
	BPF_MOD_CALL,
	BPF_MOD_JUMP,
};

int bpf_arch_text_poke(void *ip, enum bpf_text_poke_type t,
		       void *addr1, void *addr2);

void bpf_arch_poke_desc_update(struct bpf_jit_poke_descriptor *poke,
			       struct bpf_prog *new, struct bpf_prog *old);

void *bpf_arch_text_copy(void *dst, void *src, size_t len);
int bpf_arch_text_invalidate(void *dst, size_t len);

struct btf_id_set;
bool btf_id_set_contains(const struct btf_id_set *set, u32 id);

#define MAX_BPRINTF_VARARGS		12

int bpf_bprintf_prepare(char *fmt, u32 fmt_size, const u64 *raw_args,
			u32 **bin_buf, u32 num_args);
void bpf_bprintf_cleanup(void);

/* the implementation of the opaque uapi struct bpf_dynptr */
struct bpf_dynptr_kern {
	void *data;
	/* Size represents the number of usable bytes of dynptr data.
	 * If for example the offset is at 4 for a local dynptr whose data is
	 * of type u64, the number of usable bytes is 4.
	 *
	 * The upper 8 bits are reserved. It is as follows:
	 * Bits 0 - 23 = size
	 * Bits 24 - 30 = dynptr type
	 * Bit 31 = whether dynptr is read-only
	 */
	u32 size;
	u32 offset;
} __aligned(8);

enum bpf_dynptr_type {
	BPF_DYNPTR_TYPE_INVALID,
	/* Points to memory that is local to the bpf program */
	BPF_DYNPTR_TYPE_LOCAL,
	/* Underlying data is a kernel-produced ringbuf record */
	BPF_DYNPTR_TYPE_RINGBUF,
};

void bpf_dynptr_init(struct bpf_dynptr_kern *ptr, void *data,
		     enum bpf_dynptr_type type, u32 offset, u32 size);
void bpf_dynptr_set_null(struct bpf_dynptr_kern *ptr);
int bpf_dynptr_check_size(u32 size);
u32 bpf_dynptr_get_size(struct bpf_dynptr_kern *ptr);

#ifdef CONFIG_BPF_LSM
void bpf_cgroup_atype_get(u32 attach_btf_id, int cgroup_atype);
void bpf_cgroup_atype_put(int cgroup_atype);
#else
static inline void bpf_cgroup_atype_get(u32 attach_btf_id, int cgroup_atype) {}
static inline void bpf_cgroup_atype_put(int cgroup_atype) {}
#endif /* CONFIG_BPF_LSM */

struct key;

#ifdef CONFIG_KEYS
struct bpf_key {
	struct key *key;
	bool has_ref;
};
#endif /* CONFIG_KEYS */
#endif /* _LINUX_BPF_H */
