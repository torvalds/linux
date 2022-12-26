/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#ifndef _LINUX_BTF_H
#define _LINUX_BTF_H 1

#include <linux/types.h>
#include <linux/bpfptr.h>
#include <linux/bsearch.h>
#include <linux/btf_ids.h>
#include <uapi/linux/btf.h>
#include <uapi/linux/bpf.h>

#define BTF_TYPE_EMIT(type) ((void)(type *)0)
#define BTF_TYPE_EMIT_ENUM(enum_val) ((void)enum_val)

/* These need to be macros, as the expressions are used in assembler input */
#define KF_ACQUIRE	(1 << 0) /* kfunc is an acquire function */
#define KF_RELEASE	(1 << 1) /* kfunc is a release function */
#define KF_RET_NULL	(1 << 2) /* kfunc returns a pointer that may be NULL */
#define KF_KPTR_GET	(1 << 3) /* kfunc returns reference to a kptr */
/* Trusted arguments are those which are guaranteed to be valid when passed to
 * the kfunc. It is used to enforce that pointers obtained from either acquire
 * kfuncs, or from the main kernel on a tracepoint or struct_ops callback
 * invocation, remain unmodified when being passed to helpers taking trusted
 * args.
 *
 * Consider, for example, the following new task tracepoint:
 *
 *	SEC("tp_btf/task_newtask")
 *	int BPF_PROG(new_task_tp, struct task_struct *task, u64 clone_flags)
 *	{
 *		...
 *	}
 *
 * And the following kfunc:
 *
 *	BTF_ID_FLAGS(func, bpf_task_acquire, KF_ACQUIRE | KF_TRUSTED_ARGS)
 *
 * All invocations to the kfunc must pass the unmodified, unwalked task:
 *
 *	bpf_task_acquire(task);		    // Allowed
 *	bpf_task_acquire(task->last_wakee); // Rejected, walked task
 *
 * Programs may also pass referenced tasks directly to the kfunc:
 *
 *	struct task_struct *acquired;
 *
 *	acquired = bpf_task_acquire(task);	// Allowed, same as above
 *	bpf_task_acquire(acquired);		// Allowed
 *	bpf_task_acquire(task);			// Allowed
 *	bpf_task_acquire(acquired->last_wakee); // Rejected, walked task
 *
 * Programs may _not_, however, pass a task from an arbitrary fentry/fexit, or
 * kprobe/kretprobe to the kfunc, as BPF cannot guarantee that all of these
 * pointers are guaranteed to be safe. For example, the following BPF program
 * would be rejected:
 *
 * SEC("kretprobe/free_task")
 * int BPF_PROG(free_task_probe, struct task_struct *tsk)
 * {
 *	struct task_struct *acquired;
 *
 *	acquired = bpf_task_acquire(acquired); // Rejected, not a trusted pointer
 *	bpf_task_release(acquired);
 *
 *	return 0;
 * }
 */
#define KF_TRUSTED_ARGS (1 << 4) /* kfunc only takes trusted pointer arguments */
#define KF_SLEEPABLE    (1 << 5) /* kfunc may sleep */
#define KF_DESTRUCTIVE  (1 << 6) /* kfunc performs destructive actions */
#define KF_RCU          (1 << 7) /* kfunc only takes rcu pointer arguments */

/*
 * Return the name of the passed struct, if exists, or halt the build if for
 * example the structure gets renamed. In this way, developers have to revisit
 * the code using that structure name, and update it accordingly.
 */
#define stringify_struct(x)			\
	({ BUILD_BUG_ON(sizeof(struct x) < 0);	\
	   __stringify(x); })

struct btf;
struct btf_member;
struct btf_type;
union bpf_attr;
struct btf_show;
struct btf_id_set;

struct btf_kfunc_id_set {
	struct module *owner;
	struct btf_id_set8 *set;
};

struct btf_id_dtor_kfunc {
	u32 btf_id;
	u32 kfunc_btf_id;
};

struct btf_struct_meta {
	u32 btf_id;
	struct btf_record *record;
	struct btf_field_offs *field_offs;
};

struct btf_struct_metas {
	u32 cnt;
	struct btf_struct_meta types[];
};

typedef void (*btf_dtor_kfunc_t)(void *);

extern const struct file_operations btf_fops;

void btf_get(struct btf *btf);
void btf_put(struct btf *btf);
int btf_new_fd(const union bpf_attr *attr, bpfptr_t uattr);
struct btf *btf_get_by_fd(int fd);
int btf_get_info_by_fd(const struct btf *btf,
		       const union bpf_attr *attr,
		       union bpf_attr __user *uattr);
/* Figure out the size of a type_id.  If type_id is a modifier
 * (e.g. const), it will be resolved to find out the type with size.
 *
 * For example:
 * In describing "const void *",  type_id is "const" and "const"
 * refers to "void *".  The return type will be "void *".
 *
 * If type_id is a simple "int", then return type will be "int".
 *
 * @btf: struct btf object
 * @type_id: Find out the size of type_id. The type_id of the return
 *           type is set to *type_id.
 * @ret_size: It can be NULL.  If not NULL, the size of the return
 *            type is set to *ret_size.
 * Return: The btf_type (resolved to another type with size info if needed).
 *         NULL is returned if type_id itself does not have size info
 *         (e.g. void) or it cannot be resolved to another type that
 *         has size info.
 *         *type_id and *ret_size will not be changed in the
 *         NULL return case.
 */
const struct btf_type *btf_type_id_size(const struct btf *btf,
					u32 *type_id,
					u32 *ret_size);

/*
 * Options to control show behaviour.
 *	- BTF_SHOW_COMPACT: no formatting around type information
 *	- BTF_SHOW_NONAME: no struct/union member names/types
 *	- BTF_SHOW_PTR_RAW: show raw (unobfuscated) pointer values;
 *	  equivalent to %px.
 *	- BTF_SHOW_ZERO: show zero-valued struct/union members; they
 *	  are not displayed by default
 *	- BTF_SHOW_UNSAFE: skip use of bpf_probe_read() to safely read
 *	  data before displaying it.
 */
#define BTF_SHOW_COMPACT	BTF_F_COMPACT
#define BTF_SHOW_NONAME		BTF_F_NONAME
#define BTF_SHOW_PTR_RAW	BTF_F_PTR_RAW
#define BTF_SHOW_ZERO		BTF_F_ZERO
#define BTF_SHOW_UNSAFE		(1ULL << 4)

void btf_type_seq_show(const struct btf *btf, u32 type_id, void *obj,
		       struct seq_file *m);
int btf_type_seq_show_flags(const struct btf *btf, u32 type_id, void *obj,
			    struct seq_file *m, u64 flags);

/*
 * Copy len bytes of string representation of obj of BTF type_id into buf.
 *
 * @btf: struct btf object
 * @type_id: type id of type obj points to
 * @obj: pointer to typed data
 * @buf: buffer to write to
 * @len: maximum length to write to buf
 * @flags: show options (see above)
 *
 * Return: length that would have been/was copied as per snprintf, or
 *	   negative error.
 */
int btf_type_snprintf_show(const struct btf *btf, u32 type_id, void *obj,
			   char *buf, int len, u64 flags);

int btf_get_fd_by_id(u32 id);
u32 btf_obj_id(const struct btf *btf);
bool btf_is_kernel(const struct btf *btf);
bool btf_is_module(const struct btf *btf);
struct module *btf_try_get_module(const struct btf *btf);
u32 btf_nr_types(const struct btf *btf);
bool btf_member_is_reg_int(const struct btf *btf, const struct btf_type *s,
			   const struct btf_member *m,
			   u32 expected_offset, u32 expected_size);
int btf_find_spin_lock(const struct btf *btf, const struct btf_type *t);
int btf_find_timer(const struct btf *btf, const struct btf_type *t);
struct btf_record *btf_parse_fields(const struct btf *btf, const struct btf_type *t,
				    u32 field_mask, u32 value_size);
int btf_check_and_fixup_fields(const struct btf *btf, struct btf_record *rec);
struct btf_field_offs *btf_parse_field_offs(struct btf_record *rec);
bool btf_type_is_void(const struct btf_type *t);
s32 btf_find_by_name_kind(const struct btf *btf, const char *name, u8 kind);
const struct btf_type *btf_type_skip_modifiers(const struct btf *btf,
					       u32 id, u32 *res_id);
const struct btf_type *btf_type_resolve_ptr(const struct btf *btf,
					    u32 id, u32 *res_id);
const struct btf_type *btf_type_resolve_func_ptr(const struct btf *btf,
						 u32 id, u32 *res_id);
const struct btf_type *
btf_resolve_size(const struct btf *btf, const struct btf_type *type,
		 u32 *type_size);
const char *btf_type_str(const struct btf_type *t);

#define for_each_member(i, struct_type, member)			\
	for (i = 0, member = btf_type_member(struct_type);	\
	     i < btf_type_vlen(struct_type);			\
	     i++, member++)

#define for_each_vsi(i, datasec_type, member)			\
	for (i = 0, member = btf_type_var_secinfo(datasec_type);	\
	     i < btf_type_vlen(datasec_type);			\
	     i++, member++)

static inline bool btf_type_is_ptr(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_PTR;
}

static inline bool btf_type_is_int(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_INT;
}

static inline bool btf_type_is_small_int(const struct btf_type *t)
{
	return btf_type_is_int(t) && t->size <= sizeof(u64);
}

static inline bool btf_type_is_enum(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_ENUM;
}

static inline bool btf_is_any_enum(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_ENUM ||
	       BTF_INFO_KIND(t->info) == BTF_KIND_ENUM64;
}

static inline bool btf_kind_core_compat(const struct btf_type *t1,
					const struct btf_type *t2)
{
	return BTF_INFO_KIND(t1->info) == BTF_INFO_KIND(t2->info) ||
	       (btf_is_any_enum(t1) && btf_is_any_enum(t2));
}

static inline bool str_is_empty(const char *s)
{
	return !s || !s[0];
}

static inline u16 btf_kind(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info);
}

static inline bool btf_is_enum(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_ENUM;
}

static inline bool btf_is_enum64(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_ENUM64;
}

static inline u64 btf_enum64_value(const struct btf_enum64 *e)
{
	return ((u64)e->val_hi32 << 32) | e->val_lo32;
}

static inline bool btf_is_composite(const struct btf_type *t)
{
	u16 kind = btf_kind(t);

	return kind == BTF_KIND_STRUCT || kind == BTF_KIND_UNION;
}

static inline bool btf_is_array(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_ARRAY;
}

static inline bool btf_is_int(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_INT;
}

static inline bool btf_is_ptr(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_PTR;
}

static inline u8 btf_int_offset(const struct btf_type *t)
{
	return BTF_INT_OFFSET(*(u32 *)(t + 1));
}

static inline u8 btf_int_encoding(const struct btf_type *t)
{
	return BTF_INT_ENCODING(*(u32 *)(t + 1));
}

static inline bool btf_type_is_scalar(const struct btf_type *t)
{
	return btf_type_is_int(t) || btf_type_is_enum(t);
}

static inline bool btf_type_is_typedef(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_TYPEDEF;
}

static inline bool btf_type_is_volatile(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_VOLATILE;
}

static inline bool btf_type_is_func(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_FUNC;
}

static inline bool btf_type_is_func_proto(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_FUNC_PROTO;
}

static inline bool btf_type_is_var(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_VAR;
}

static inline bool btf_type_is_type_tag(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_TYPE_TAG;
}

/* union is only a special case of struct:
 * all its offsetof(member) == 0
 */
static inline bool btf_type_is_struct(const struct btf_type *t)
{
	u8 kind = BTF_INFO_KIND(t->info);

	return kind == BTF_KIND_STRUCT || kind == BTF_KIND_UNION;
}

static inline bool __btf_type_is_struct(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_STRUCT;
}

static inline bool btf_type_is_array(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info) == BTF_KIND_ARRAY;
}

static inline u16 btf_type_vlen(const struct btf_type *t)
{
	return BTF_INFO_VLEN(t->info);
}

static inline u16 btf_vlen(const struct btf_type *t)
{
	return btf_type_vlen(t);
}

static inline u16 btf_func_linkage(const struct btf_type *t)
{
	return BTF_INFO_VLEN(t->info);
}

static inline bool btf_type_kflag(const struct btf_type *t)
{
	return BTF_INFO_KFLAG(t->info);
}

static inline u32 __btf_member_bit_offset(const struct btf_type *struct_type,
					  const struct btf_member *member)
{
	return btf_type_kflag(struct_type) ? BTF_MEMBER_BIT_OFFSET(member->offset)
					   : member->offset;
}

static inline u32 __btf_member_bitfield_size(const struct btf_type *struct_type,
					     const struct btf_member *member)
{
	return btf_type_kflag(struct_type) ? BTF_MEMBER_BITFIELD_SIZE(member->offset)
					   : 0;
}

static inline struct btf_member *btf_members(const struct btf_type *t)
{
	return (struct btf_member *)(t + 1);
}

static inline u32 btf_member_bit_offset(const struct btf_type *t, u32 member_idx)
{
	const struct btf_member *m = btf_members(t) + member_idx;

	return __btf_member_bit_offset(t, m);
}

static inline u32 btf_member_bitfield_size(const struct btf_type *t, u32 member_idx)
{
	const struct btf_member *m = btf_members(t) + member_idx;

	return __btf_member_bitfield_size(t, m);
}

static inline const struct btf_member *btf_type_member(const struct btf_type *t)
{
	return (const struct btf_member *)(t + 1);
}

static inline struct btf_array *btf_array(const struct btf_type *t)
{
	return (struct btf_array *)(t + 1);
}

static inline struct btf_enum *btf_enum(const struct btf_type *t)
{
	return (struct btf_enum *)(t + 1);
}

static inline struct btf_enum64 *btf_enum64(const struct btf_type *t)
{
	return (struct btf_enum64 *)(t + 1);
}

static inline const struct btf_var_secinfo *btf_type_var_secinfo(
		const struct btf_type *t)
{
	return (const struct btf_var_secinfo *)(t + 1);
}

static inline struct btf_param *btf_params(const struct btf_type *t)
{
	return (struct btf_param *)(t + 1);
}

static inline int btf_id_cmp_func(const void *a, const void *b)
{
	const int *pa = a, *pb = b;

	return *pa - *pb;
}

static inline bool btf_id_set_contains(const struct btf_id_set *set, u32 id)
{
	return bsearch(&id, set->ids, set->cnt, sizeof(u32), btf_id_cmp_func) != NULL;
}

static inline void *btf_id_set8_contains(const struct btf_id_set8 *set, u32 id)
{
	return bsearch(&id, set->pairs, set->cnt, sizeof(set->pairs[0]), btf_id_cmp_func);
}

struct bpf_prog;
struct bpf_verifier_log;

#ifdef CONFIG_BPF_SYSCALL
const struct btf_type *btf_type_by_id(const struct btf *btf, u32 type_id);
const char *btf_name_by_offset(const struct btf *btf, u32 offset);
struct btf *btf_parse_vmlinux(void);
struct btf *bpf_prog_get_target_btf(const struct bpf_prog *prog);
u32 *btf_kfunc_id_set_contains(const struct btf *btf,
			       enum bpf_prog_type prog_type,
			       u32 kfunc_btf_id);
u32 *btf_kfunc_is_modify_return(const struct btf *btf, u32 kfunc_btf_id);
int register_btf_kfunc_id_set(enum bpf_prog_type prog_type,
			      const struct btf_kfunc_id_set *s);
int register_btf_fmodret_id_set(const struct btf_kfunc_id_set *kset);
s32 btf_find_dtor_kfunc(struct btf *btf, u32 btf_id);
int register_btf_id_dtor_kfuncs(const struct btf_id_dtor_kfunc *dtors, u32 add_cnt,
				struct module *owner);
struct btf_struct_meta *btf_find_struct_meta(const struct btf *btf, u32 btf_id);
const struct btf_member *
btf_get_prog_ctx_type(struct bpf_verifier_log *log, const struct btf *btf,
		      const struct btf_type *t, enum bpf_prog_type prog_type,
		      int arg);
int get_kern_ctx_btf_id(struct bpf_verifier_log *log, enum bpf_prog_type prog_type);
bool btf_types_are_same(const struct btf *btf1, u32 id1,
			const struct btf *btf2, u32 id2);
#else
static inline const struct btf_type *btf_type_by_id(const struct btf *btf,
						    u32 type_id)
{
	return NULL;
}
static inline const char *btf_name_by_offset(const struct btf *btf,
					     u32 offset)
{
	return NULL;
}
static inline u32 *btf_kfunc_id_set_contains(const struct btf *btf,
					     enum bpf_prog_type prog_type,
					     u32 kfunc_btf_id)
{
	return NULL;
}
static inline int register_btf_kfunc_id_set(enum bpf_prog_type prog_type,
					    const struct btf_kfunc_id_set *s)
{
	return 0;
}
static inline s32 btf_find_dtor_kfunc(struct btf *btf, u32 btf_id)
{
	return -ENOENT;
}
static inline int register_btf_id_dtor_kfuncs(const struct btf_id_dtor_kfunc *dtors,
					      u32 add_cnt, struct module *owner)
{
	return 0;
}
static inline struct btf_struct_meta *btf_find_struct_meta(const struct btf *btf, u32 btf_id)
{
	return NULL;
}
static inline const struct btf_member *
btf_get_prog_ctx_type(struct bpf_verifier_log *log, const struct btf *btf,
		      const struct btf_type *t, enum bpf_prog_type prog_type,
		      int arg)
{
	return NULL;
}
static inline int get_kern_ctx_btf_id(struct bpf_verifier_log *log,
				      enum bpf_prog_type prog_type) {
	return -EINVAL;
}
static inline bool btf_types_are_same(const struct btf *btf1, u32 id1,
				      const struct btf *btf2, u32 id2)
{
	return false;
}
#endif

static inline bool btf_type_is_struct_ptr(struct btf *btf, const struct btf_type *t)
{
	if (!btf_type_is_ptr(t))
		return false;

	t = btf_type_skip_modifiers(btf, t->type, NULL);

	return btf_type_is_struct(t);
}

#endif
