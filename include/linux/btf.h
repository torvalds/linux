/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#ifndef _LINUX_BTF_H
#define _LINUX_BTF_H 1

#include <linux/types.h>
#include <uapi/linux/btf.h>
#include <uapi/linux/bpf.h>

#define BTF_TYPE_EMIT(type) ((void)(type *)0)

struct btf;
struct btf_member;
struct btf_type;
union bpf_attr;
struct btf_show;

extern const struct file_operations btf_fops;

void btf_put(struct btf *btf);
int btf_new_fd(const union bpf_attr *attr);
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
u32 btf_id(const struct btf *btf);
bool btf_member_is_reg_int(const struct btf *btf, const struct btf_type *s,
			   const struct btf_member *m,
			   u32 expected_offset, u32 expected_size);
int btf_find_spin_lock(const struct btf *btf, const struct btf_type *t);
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

static inline u16 btf_func_linkage(const struct btf_type *t)
{
	return BTF_INFO_VLEN(t->info);
}

static inline bool btf_type_kflag(const struct btf_type *t)
{
	return BTF_INFO_KFLAG(t->info);
}

static inline u32 btf_member_bit_offset(const struct btf_type *struct_type,
					const struct btf_member *member)
{
	return btf_type_kflag(struct_type) ? BTF_MEMBER_BIT_OFFSET(member->offset)
					   : member->offset;
}

static inline u32 btf_member_bitfield_size(const struct btf_type *struct_type,
					   const struct btf_member *member)
{
	return btf_type_kflag(struct_type) ? BTF_MEMBER_BITFIELD_SIZE(member->offset)
					   : 0;
}

static inline const struct btf_member *btf_type_member(const struct btf_type *t)
{
	return (const struct btf_member *)(t + 1);
}

static inline const struct btf_var_secinfo *btf_type_var_secinfo(
		const struct btf_type *t)
{
	return (const struct btf_var_secinfo *)(t + 1);
}

#ifdef CONFIG_BPF_SYSCALL
const struct btf_type *btf_type_by_id(const struct btf *btf, u32 type_id);
const char *btf_name_by_offset(const struct btf *btf, u32 offset);
struct btf *btf_parse_vmlinux(void);
struct btf *bpf_prog_get_target_btf(const struct bpf_prog *prog);
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
#endif

#endif
