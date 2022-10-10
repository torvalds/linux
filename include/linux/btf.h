/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#ifndef _LINUX_BTF_H
#define _LINUX_BTF_H 1

#include <linux/types.h>
#include <linux/bpfptr.h>
#include <uapi/linux/btf.h>
#include <uapi/linux/bpf.h>

#define BTF_TYPE_EMIT(type) ((void)(type *)0)
#define BTF_TYPE_EMIT_ENUM(enum_val) ((void)enum_val)

/* These need to be macros, as the expressions are used in assembler input */
#define KF_ACQUIRE	(1 << 0) /* kfunc is an acquire function */
#define KF_RELEASE	(1 << 1) /* kfunc is a release function */
#define KF_RET_NULL	(1 << 2) /* kfunc returns a pointer that may be NULL */
#define KF_KPTR_GET	(1 << 3) /* kfunc returns reference to a kptr */
/* Trusted arguments are those which are meant to be referenced arguments with
 * unchanged offset. It is used to enforce that pointers obtained from acquire
 * kfuncs remain unmodified when being passed to helpers taking trusted args.
 *
 * Consider
 *	struct foo {
 *		int data;
 *		struct foo *next;
 *	};
 *
 *	struct bar {
 *		int data;
 *		struct foo f;
 *	};
 *
 *	struct foo *f = alloc_foo(); // Acquire kfunc
 *	struct bar *b = alloc_bar(); // Acquire kfunc
 *
 * If a kfunc set_foo_data() wants to operate only on the allocated object, it
 * will set the KF_TRUSTED_ARGS flag, which will prevent unsafe usage like:
 *
 *	set_foo_data(f, 42);	   // Allowed
 *	set_foo_data(f->next, 42); // Rejected, non-referenced pointer
 *	set_foo_data(&f->next, 42);// Rejected, referenced, but wrong type
 *	set_foo_data(&b->f, 42);   // Rejected, referenced, but bad offset
 *
 * In the final case, usually for the purposes of type matching, it is deduced
 * by looking at the type of the member at the offset, but due to the
 * requirement of trusted argument, this deduction will be strict and not done
 * for this case.
 */
#define KF_TRUSTED_ARGS (1 << 4) /* kfunc only takes trusted pointer arguments */
#define KF_SLEEPABLE    (1 << 5) /* kfunc may sleep */
#define KF_DESTRUCTIVE  (1 << 6) /* kfunc performs destructive actions */

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
struct bpf_map_value_off *btf_parse_kptrs(const struct btf *btf,
					  const struct btf_type *t);
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

#ifdef CONFIG_BPF_SYSCALL
struct bpf_prog;

const struct btf_type *btf_type_by_id(const struct btf *btf, u32 type_id);
const char *btf_name_by_offset(const struct btf *btf, u32 offset);
struct btf *btf_parse_vmlinux(void);
struct btf *bpf_prog_get_target_btf(const struct bpf_prog *prog);
u32 *btf_kfunc_id_set_contains(const struct btf *btf,
			       enum bpf_prog_type prog_type,
			       u32 kfunc_btf_id);
int register_btf_kfunc_id_set(enum bpf_prog_type prog_type,
			      const struct btf_kfunc_id_set *s);
s32 btf_find_dtor_kfunc(struct btf *btf, u32 btf_id);
int register_btf_id_dtor_kfuncs(const struct btf_id_dtor_kfunc *dtors, u32 add_cnt,
				struct module *owner);
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
#endif

static inline bool btf_type_is_struct_ptr(struct btf *btf, const struct btf_type *t)
{
	if (!btf_type_is_ptr(t))
		return false;

	t = btf_type_skip_modifiers(btf, t->type, NULL);

	return btf_type_is_struct(t);
}

#endif
