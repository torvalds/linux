/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2018 Facebook */

#include <uapi/linux/btf.h>
#include <uapi/linux/types.h>
#include <linux/compiler.h>
#include <linux/errno.h>
#include <linux/slab.h>
#include <linux/uaccess.h>
#include <linux/kernel.h>
#include <linux/bpf_verifier.h>
#include <linux/btf.h>

/* BTF (BPF Type Format) is the meta data format which describes
 * the data types of BPF program/map.  Hence, it basically focus
 * on the C programming language which the modern BPF is primary
 * using.
 *
 * ELF Section:
 * ~~~~~~~~~~~
 * The BTF data is stored under the ".BTF" ELF section
 *
 * struct btf_type:
 * ~~~~~~~~~~~~~~~
 * Each 'struct btf_type' object describes a C data type.
 * Depending on the type it is describing, a 'struct btf_type'
 * object may be followed by more data.  F.e.
 * To describe an array, 'struct btf_type' is followed by
 * 'struct btf_array'.
 *
 * 'struct btf_type' and any extra data following it are
 * 4 bytes aligned.
 *
 * Type section:
 * ~~~~~~~~~~~~~
 * The BTF type section contains a list of 'struct btf_type' objects.
 * Each one describes a C type.  Recall from the above section
 * that a 'struct btf_type' object could be immediately followed by extra
 * data in order to desribe some particular C types.
 *
 * type_id:
 * ~~~~~~~
 * Each btf_type object is identified by a type_id.  The type_id
 * is implicitly implied by the location of the btf_type object in
 * the BTF type section.  The first one has type_id 1.  The second
 * one has type_id 2...etc.  Hence, an earlier btf_type has
 * a smaller type_id.
 *
 * A btf_type object may refer to another btf_type object by using
 * type_id (i.e. the "type" in the "struct btf_type").
 *
 * NOTE that we cannot assume any reference-order.
 * A btf_type object can refer to an earlier btf_type object
 * but it can also refer to a later btf_type object.
 *
 * For example, to describe "const void *".  A btf_type
 * object describing "const" may refer to another btf_type
 * object describing "void *".  This type-reference is done
 * by specifying type_id:
 *
 * [1] CONST (anon) type_id=2
 * [2] PTR (anon) type_id=0
 *
 * The above is the btf_verifier debug log:
 *   - Each line started with "[?]" is a btf_type object
 *   - [?] is the type_id of the btf_type object.
 *   - CONST/PTR is the BTF_KIND_XXX
 *   - "(anon)" is the name of the type.  It just
 *     happens that CONST and PTR has no name.
 *   - type_id=XXX is the 'u32 type' in btf_type
 *
 * NOTE: "void" has type_id 0
 *
 * String section:
 * ~~~~~~~~~~~~~~
 * The BTF string section contains the names used by the type section.
 * Each string is referred by an "offset" from the beginning of the
 * string section.
 *
 * Each string is '\0' terminated.
 *
 * The first character in the string section must be '\0'
 * which is used to mean 'anonymous'. Some btf_type may not
 * have a name.
 */

/* BTF verification:
 *
 * To verify BTF data, two passes are needed.
 *
 * Pass #1
 * ~~~~~~~
 * The first pass is to collect all btf_type objects to
 * an array: "btf->types".
 *
 * Depending on the C type that a btf_type is describing,
 * a btf_type may be followed by extra data.  We don't know
 * how many btf_type is there, and more importantly we don't
 * know where each btf_type is located in the type section.
 *
 * Without knowing the location of each type_id, most verifications
 * cannot be done.  e.g. an earlier btf_type may refer to a later
 * btf_type (recall the "const void *" above), so we cannot
 * check this type-reference in the first pass.
 *
 * In the first pass, it still does some verifications (e.g.
 * checking the name is a valid offset to the string section).
 */

#define BITS_PER_U64 (sizeof(u64) * BITS_PER_BYTE)
#define BITS_PER_BYTE_MASK (BITS_PER_BYTE - 1)
#define BITS_PER_BYTE_MASKED(bits) ((bits) & BITS_PER_BYTE_MASK)
#define BITS_ROUNDDOWN_BYTES(bits) ((bits) >> 3)
#define BITS_ROUNDUP_BYTES(bits) \
	(BITS_ROUNDDOWN_BYTES(bits) + !!BITS_PER_BYTE_MASKED(bits))

/* 16MB for 64k structs and each has 16 members and
 * a few MB spaces for the string section.
 * The hard limit is S32_MAX.
 */
#define BTF_MAX_SIZE (16 * 1024 * 1024)
/* 64k. We can raise it later. The hard limit is S32_MAX. */
#define BTF_MAX_NR_TYPES 65535

#define for_each_member(i, struct_type, member)			\
	for (i = 0, member = btf_type_member(struct_type);	\
	     i < btf_type_vlen(struct_type);			\
	     i++, member++)

struct btf {
	union {
		struct btf_header *hdr;
		void *data;
	};
	struct btf_type **types;
	const char *strings;
	void *nohdr_data;
	u32 nr_types;
	u32 types_size;
	u32 data_size;
};

struct btf_verifier_env {
	struct btf *btf;
	struct bpf_verifier_log log;
	u32 log_type_id;
};

static const char * const btf_kind_str[NR_BTF_KINDS] = {
	[BTF_KIND_UNKN]		= "UNKNOWN",
	[BTF_KIND_INT]		= "INT",
	[BTF_KIND_PTR]		= "PTR",
	[BTF_KIND_ARRAY]	= "ARRAY",
	[BTF_KIND_STRUCT]	= "STRUCT",
	[BTF_KIND_UNION]	= "UNION",
	[BTF_KIND_ENUM]		= "ENUM",
	[BTF_KIND_FWD]		= "FWD",
	[BTF_KIND_TYPEDEF]	= "TYPEDEF",
	[BTF_KIND_VOLATILE]	= "VOLATILE",
	[BTF_KIND_CONST]	= "CONST",
	[BTF_KIND_RESTRICT]	= "RESTRICT",
};

struct btf_kind_operations {
	s32 (*check_meta)(struct btf_verifier_env *env,
			  const struct btf_type *t,
			  u32 meta_left);
	void (*log_details)(struct btf_verifier_env *env,
			    const struct btf_type *t);
};

static const struct btf_kind_operations * const kind_ops[NR_BTF_KINDS];
static struct btf_type btf_void;

static const char *btf_int_encoding_str(u8 encoding)
{
	if (encoding == 0)
		return "(none)";
	else if (encoding == BTF_INT_SIGNED)
		return "SIGNED";
	else if (encoding == BTF_INT_CHAR)
		return "CHAR";
	else if (encoding == BTF_INT_BOOL)
		return "BOOL";
	else if (encoding == BTF_INT_VARARGS)
		return "VARARGS";
	else
		return "UNKN";
}

static u16 btf_type_vlen(const struct btf_type *t)
{
	return BTF_INFO_VLEN(t->info);
}

static u32 btf_type_int(const struct btf_type *t)
{
	return *(u32 *)(t + 1);
}

static const struct btf_array *btf_type_array(const struct btf_type *t)
{
	return (const struct btf_array *)(t + 1);
}

static const struct btf_member *btf_type_member(const struct btf_type *t)
{
	return (const struct btf_member *)(t + 1);
}

static const struct btf_enum *btf_type_enum(const struct btf_type *t)
{
	return (const struct btf_enum *)(t + 1);
}

static const struct btf_kind_operations *btf_type_ops(const struct btf_type *t)
{
	return kind_ops[BTF_INFO_KIND(t->info)];
}

static bool btf_name_offset_valid(const struct btf *btf, u32 offset)
{
	return !BTF_STR_TBL_ELF_ID(offset) &&
		BTF_STR_OFFSET(offset) < btf->hdr->str_len;
}

static const char *btf_name_by_offset(const struct btf *btf, u32 offset)
{
	if (!BTF_STR_OFFSET(offset))
		return "(anon)";
	else if (BTF_STR_OFFSET(offset) < btf->hdr->str_len)
		return &btf->strings[BTF_STR_OFFSET(offset)];
	else
		return "(invalid-name-offset)";
}

__printf(2, 3) static void __btf_verifier_log(struct bpf_verifier_log *log,
					      const char *fmt, ...)
{
	va_list args;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}

__printf(2, 3) static void btf_verifier_log(struct btf_verifier_env *env,
					    const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	va_start(args, fmt);
	bpf_verifier_vlog(log, fmt, args);
	va_end(args);
}

__printf(4, 5) static void __btf_verifier_log_type(struct btf_verifier_env *env,
						   const struct btf_type *t,
						   bool log_details,
						   const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	u8 kind = BTF_INFO_KIND(t->info);
	struct btf *btf = env->btf;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	__btf_verifier_log(log, "[%u] %s %s%s",
			   env->log_type_id,
			   btf_kind_str[kind],
			   btf_name_by_offset(btf, t->name),
			   log_details ? " " : "");

	if (log_details)
		btf_type_ops(t)->log_details(env, t);

	if (fmt && *fmt) {
		__btf_verifier_log(log, " ");
		va_start(args, fmt);
		bpf_verifier_vlog(log, fmt, args);
		va_end(args);
	}

	__btf_verifier_log(log, "\n");
}

#define btf_verifier_log_type(env, t, ...) \
	__btf_verifier_log_type((env), (t), true, __VA_ARGS__)
#define btf_verifier_log_basic(env, t, ...) \
	__btf_verifier_log_type((env), (t), false, __VA_ARGS__)

__printf(4, 5)
static void btf_verifier_log_member(struct btf_verifier_env *env,
				    const struct btf_type *struct_type,
				    const struct btf_member *member,
				    const char *fmt, ...)
{
	struct bpf_verifier_log *log = &env->log;
	struct btf *btf = env->btf;
	va_list args;

	if (!bpf_verifier_log_needed(log))
		return;

	__btf_verifier_log(log, "\t%s type_id=%u bits_offset=%u",
			   btf_name_by_offset(btf, member->name),
			   member->type, member->offset);

	if (fmt && *fmt) {
		__btf_verifier_log(log, " ");
		va_start(args, fmt);
		bpf_verifier_vlog(log, fmt, args);
		va_end(args);
	}

	__btf_verifier_log(log, "\n");
}

static void btf_verifier_log_hdr(struct btf_verifier_env *env)
{
	struct bpf_verifier_log *log = &env->log;
	const struct btf *btf = env->btf;
	const struct btf_header *hdr;

	if (!bpf_verifier_log_needed(log))
		return;

	hdr = btf->hdr;
	__btf_verifier_log(log, "magic: 0x%x\n", hdr->magic);
	__btf_verifier_log(log, "version: %u\n", hdr->version);
	__btf_verifier_log(log, "flags: 0x%x\n", hdr->flags);
	__btf_verifier_log(log, "parent_label: %u\n", hdr->parent_label);
	__btf_verifier_log(log, "parent_name: %u\n", hdr->parent_name);
	__btf_verifier_log(log, "label_off: %u\n", hdr->label_off);
	__btf_verifier_log(log, "object_off: %u\n", hdr->object_off);
	__btf_verifier_log(log, "func_off: %u\n", hdr->func_off);
	__btf_verifier_log(log, "type_off: %u\n", hdr->type_off);
	__btf_verifier_log(log, "str_off: %u\n", hdr->str_off);
	__btf_verifier_log(log, "str_len: %u\n", hdr->str_len);
	__btf_verifier_log(log, "btf_total_size: %u\n", btf->data_size);
}

static int btf_add_type(struct btf_verifier_env *env, struct btf_type *t)
{
	struct btf *btf = env->btf;

	/* < 2 because +1 for btf_void which is always in btf->types[0].
	 * btf_void is not accounted in btf->nr_types because btf_void
	 * does not come from the BTF file.
	 */
	if (btf->types_size - btf->nr_types < 2) {
		/* Expand 'types' array */

		struct btf_type **new_types;
		u32 expand_by, new_size;

		if (btf->types_size == BTF_MAX_NR_TYPES) {
			btf_verifier_log(env, "Exceeded max num of types");
			return -E2BIG;
		}

		expand_by = max_t(u32, btf->types_size >> 2, 16);
		new_size = min_t(u32, BTF_MAX_NR_TYPES,
				 btf->types_size + expand_by);

		new_types = kvzalloc(new_size * sizeof(*new_types),
				     GFP_KERNEL | __GFP_NOWARN);
		if (!new_types)
			return -ENOMEM;

		if (btf->nr_types == 0)
			new_types[0] = &btf_void;
		else
			memcpy(new_types, btf->types,
			       sizeof(*btf->types) * (btf->nr_types + 1));

		kvfree(btf->types);
		btf->types = new_types;
		btf->types_size = new_size;
	}

	btf->types[++(btf->nr_types)] = t;

	return 0;
}

static void btf_free(struct btf *btf)
{
	kvfree(btf->types);
	kvfree(btf->data);
	kfree(btf);
}

static void btf_verifier_env_free(struct btf_verifier_env *env)
{
	kfree(env);
}

static s32 btf_int_check_meta(struct btf_verifier_env *env,
			      const struct btf_type *t,
			      u32 meta_left)
{
	u32 int_data, nr_bits, meta_needed = sizeof(int_data);
	u16 encoding;

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	int_data = btf_type_int(t);
	nr_bits = BTF_INT_BITS(int_data) + BTF_INT_OFFSET(int_data);

	if (nr_bits > BITS_PER_U64) {
		btf_verifier_log_type(env, t, "nr_bits exceeds %zu",
				      BITS_PER_U64);
		return -EINVAL;
	}

	if (BITS_ROUNDUP_BYTES(nr_bits) > t->size) {
		btf_verifier_log_type(env, t, "nr_bits exceeds type_size");
		return -EINVAL;
	}

	encoding = BTF_INT_ENCODING(int_data);
	if (encoding &&
	    encoding != BTF_INT_SIGNED &&
	    encoding != BTF_INT_CHAR &&
	    encoding != BTF_INT_BOOL &&
	    encoding != BTF_INT_VARARGS) {
		btf_verifier_log_type(env, t, "Unsupported encoding");
		return -ENOTSUPP;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static void btf_int_log(struct btf_verifier_env *env,
			const struct btf_type *t)
{
	int int_data = btf_type_int(t);

	btf_verifier_log(env,
			 "size=%u bits_offset=%u nr_bits=%u encoding=%s",
			 t->size, BTF_INT_OFFSET(int_data),
			 BTF_INT_BITS(int_data),
			 btf_int_encoding_str(BTF_INT_ENCODING(int_data)));
}

static const struct btf_kind_operations int_ops = {
	.check_meta = btf_int_check_meta,
	.log_details = btf_int_log,
};

static int btf_ref_type_check_meta(struct btf_verifier_env *env,
				   const struct btf_type *t,
				   u32 meta_left)
{
	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	if (BTF_TYPE_PARENT(t->type)) {
		btf_verifier_log_type(env, t, "Invalid type_id");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return 0;
}

static void btf_ref_type_log(struct btf_verifier_env *env,
			     const struct btf_type *t)
{
	btf_verifier_log(env, "type_id=%u", t->type);
}

static struct btf_kind_operations modifier_ops = {
	.check_meta = btf_ref_type_check_meta,
	.log_details = btf_ref_type_log,
};

static struct btf_kind_operations ptr_ops = {
	.check_meta = btf_ref_type_check_meta,
	.log_details = btf_ref_type_log,
};

static struct btf_kind_operations fwd_ops = {
	.check_meta = btf_ref_type_check_meta,
	.log_details = btf_ref_type_log,
};

static s32 btf_array_check_meta(struct btf_verifier_env *env,
				const struct btf_type *t,
				u32 meta_left)
{
	const struct btf_array *array = btf_type_array(t);
	u32 meta_needed = sizeof(*array);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (btf_type_vlen(t)) {
		btf_verifier_log_type(env, t, "vlen != 0");
		return -EINVAL;
	}

	/* We are a little forgiving on array->index_type since
	 * the kernel is not using it.
	 */
	/* Array elem cannot be in type void,
	 * so !array->type is not allowed.
	 */
	if (!array->type || BTF_TYPE_PARENT(array->type)) {
		btf_verifier_log_type(env, t, "Invalid type_id");
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	return meta_needed;
}

static void btf_array_log(struct btf_verifier_env *env,
			  const struct btf_type *t)
{
	const struct btf_array *array = btf_type_array(t);

	btf_verifier_log(env, "type_id=%u index_type_id=%u nr_elems=%u",
			 array->type, array->index_type, array->nelems);
}

static struct btf_kind_operations array_ops = {
	.check_meta = btf_array_check_meta,
	.log_details = btf_array_log,
};

static s32 btf_struct_check_meta(struct btf_verifier_env *env,
				 const struct btf_type *t,
				 u32 meta_left)
{
	bool is_union = BTF_INFO_KIND(t->info) == BTF_KIND_UNION;
	const struct btf_member *member;
	struct btf *btf = env->btf;
	u32 struct_size = t->size;
	u32 meta_needed;
	u16 i;

	meta_needed = btf_type_vlen(t) * sizeof(*member);
	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	for_each_member(i, t, member) {
		if (!btf_name_offset_valid(btf, member->name)) {
			btf_verifier_log_member(env, t, member,
						"Invalid member name_offset:%u",
						member->name);
			return -EINVAL;
		}

		/* A member cannot be in type void */
		if (!member->type || BTF_TYPE_PARENT(member->type)) {
			btf_verifier_log_member(env, t, member,
						"Invalid type_id");
			return -EINVAL;
		}

		if (is_union && member->offset) {
			btf_verifier_log_member(env, t, member,
						"Invalid member bits_offset");
			return -EINVAL;
		}

		if (BITS_ROUNDUP_BYTES(member->offset) > struct_size) {
			btf_verifier_log_member(env, t, member,
						"Memmber bits_offset exceeds its struct size");
			return -EINVAL;
		}

		btf_verifier_log_member(env, t, member, NULL);
	}

	return meta_needed;
}

static void btf_struct_log(struct btf_verifier_env *env,
			   const struct btf_type *t)
{
	btf_verifier_log(env, "size=%u vlen=%u", t->size, btf_type_vlen(t));
}

static struct btf_kind_operations struct_ops = {
	.check_meta = btf_struct_check_meta,
	.log_details = btf_struct_log,
};

static s32 btf_enum_check_meta(struct btf_verifier_env *env,
			       const struct btf_type *t,
			       u32 meta_left)
{
	const struct btf_enum *enums = btf_type_enum(t);
	struct btf *btf = env->btf;
	u16 i, nr_enums;
	u32 meta_needed;

	nr_enums = btf_type_vlen(t);
	meta_needed = nr_enums * sizeof(*enums);

	if (meta_left < meta_needed) {
		btf_verifier_log_basic(env, t,
				       "meta_left:%u meta_needed:%u",
				       meta_left, meta_needed);
		return -EINVAL;
	}

	if (t->size != sizeof(int)) {
		btf_verifier_log_type(env, t, "Expected size:%zu",
				      sizeof(int));
		return -EINVAL;
	}

	btf_verifier_log_type(env, t, NULL);

	for (i = 0; i < nr_enums; i++) {
		if (!btf_name_offset_valid(btf, enums[i].name)) {
			btf_verifier_log(env, "\tInvalid name_offset:%u",
					 enums[i].name);
			return -EINVAL;
		}

		btf_verifier_log(env, "\t%s val=%d\n",
				 btf_name_by_offset(btf, enums[i].name),
				 enums[i].val);
	}

	return meta_needed;
}

static void btf_enum_log(struct btf_verifier_env *env,
			 const struct btf_type *t)
{
	btf_verifier_log(env, "size=%u vlen=%u", t->size, btf_type_vlen(t));
}

static struct btf_kind_operations enum_ops = {
	.check_meta = btf_enum_check_meta,
	.log_details = btf_enum_log,
};

static const struct btf_kind_operations * const kind_ops[NR_BTF_KINDS] = {
	[BTF_KIND_INT] = &int_ops,
	[BTF_KIND_PTR] = &ptr_ops,
	[BTF_KIND_ARRAY] = &array_ops,
	[BTF_KIND_STRUCT] = &struct_ops,
	[BTF_KIND_UNION] = &struct_ops,
	[BTF_KIND_ENUM] = &enum_ops,
	[BTF_KIND_FWD] = &fwd_ops,
	[BTF_KIND_TYPEDEF] = &modifier_ops,
	[BTF_KIND_VOLATILE] = &modifier_ops,
	[BTF_KIND_CONST] = &modifier_ops,
	[BTF_KIND_RESTRICT] = &modifier_ops,
};

static s32 btf_check_meta(struct btf_verifier_env *env,
			  const struct btf_type *t,
			  u32 meta_left)
{
	u32 saved_meta_left = meta_left;
	s32 var_meta_size;

	if (meta_left < sizeof(*t)) {
		btf_verifier_log(env, "[%u] meta_left:%u meta_needed:%zu",
				 env->log_type_id, meta_left, sizeof(*t));
		return -EINVAL;
	}
	meta_left -= sizeof(*t);

	if (BTF_INFO_KIND(t->info) > BTF_KIND_MAX ||
	    BTF_INFO_KIND(t->info) == BTF_KIND_UNKN) {
		btf_verifier_log(env, "[%u] Invalid kind:%u",
				 env->log_type_id, BTF_INFO_KIND(t->info));
		return -EINVAL;
	}

	if (!btf_name_offset_valid(env->btf, t->name)) {
		btf_verifier_log(env, "[%u] Invalid name_offset:%u",
				 env->log_type_id, t->name);
		return -EINVAL;
	}

	var_meta_size = btf_type_ops(t)->check_meta(env, t, meta_left);
	if (var_meta_size < 0)
		return var_meta_size;

	meta_left -= var_meta_size;

	return saved_meta_left - meta_left;
}

static int btf_check_all_metas(struct btf_verifier_env *env)
{
	struct btf *btf = env->btf;
	struct btf_header *hdr;
	void *cur, *end;

	hdr = btf->hdr;
	cur = btf->nohdr_data + hdr->type_off;
	end = btf->nohdr_data + hdr->str_off;

	env->log_type_id = 1;
	while (cur < end) {
		struct btf_type *t = cur;
		s32 meta_size;

		meta_size = btf_check_meta(env, t, end - cur);
		if (meta_size < 0)
			return meta_size;

		btf_add_type(env, t);
		cur += meta_size;
		env->log_type_id++;
	}

	return 0;
}

static int btf_parse_type_sec(struct btf_verifier_env *env)
{
	return btf_check_all_metas(env);
}

static int btf_parse_str_sec(struct btf_verifier_env *env)
{
	const struct btf_header *hdr;
	struct btf *btf = env->btf;
	const char *start, *end;

	hdr = btf->hdr;
	start = btf->nohdr_data + hdr->str_off;
	end = start + hdr->str_len;

	if (!hdr->str_len || hdr->str_len - 1 > BTF_MAX_NAME_OFFSET ||
	    start[0] || end[-1]) {
		btf_verifier_log(env, "Invalid string section");
		return -EINVAL;
	}

	btf->strings = start;

	return 0;
}

static int btf_parse_hdr(struct btf_verifier_env *env)
{
	const struct btf_header *hdr;
	struct btf *btf = env->btf;
	u32 meta_left;

	if (btf->data_size < sizeof(*hdr)) {
		btf_verifier_log(env, "btf_header not found");
		return -EINVAL;
	}

	btf_verifier_log_hdr(env);

	hdr = btf->hdr;
	if (hdr->magic != BTF_MAGIC) {
		btf_verifier_log(env, "Invalid magic");
		return -EINVAL;
	}

	if (hdr->version != BTF_VERSION) {
		btf_verifier_log(env, "Unsupported version");
		return -ENOTSUPP;
	}

	if (hdr->flags) {
		btf_verifier_log(env, "Unsupported flags");
		return -ENOTSUPP;
	}

	meta_left = btf->data_size - sizeof(*hdr);
	if (!meta_left) {
		btf_verifier_log(env, "No data");
		return -EINVAL;
	}

	if (meta_left < hdr->type_off || hdr->str_off <= hdr->type_off ||
	    /* Type section must align to 4 bytes */
	    hdr->type_off & (sizeof(u32) - 1)) {
		btf_verifier_log(env, "Invalid type_off");
		return -EINVAL;
	}

	if (meta_left < hdr->str_off ||
	    meta_left - hdr->str_off < hdr->str_len) {
		btf_verifier_log(env, "Invalid str_off or str_len");
		return -EINVAL;
	}

	btf->nohdr_data = btf->hdr + 1;

	return 0;
}

static struct btf *btf_parse(void __user *btf_data, u32 btf_data_size,
			     u32 log_level, char __user *log_ubuf, u32 log_size)
{
	struct btf_verifier_env *env = NULL;
	struct bpf_verifier_log *log;
	struct btf *btf = NULL;
	u8 *data;
	int err;

	if (btf_data_size > BTF_MAX_SIZE)
		return ERR_PTR(-E2BIG);

	env = kzalloc(sizeof(*env), GFP_KERNEL | __GFP_NOWARN);
	if (!env)
		return ERR_PTR(-ENOMEM);

	log = &env->log;
	if (log_level || log_ubuf || log_size) {
		/* user requested verbose verifier output
		 * and supplied buffer to store the verification trace
		 */
		log->level = log_level;
		log->ubuf = log_ubuf;
		log->len_total = log_size;

		/* log attributes have to be sane */
		if (log->len_total < 128 || log->len_total > UINT_MAX >> 8 ||
		    !log->level || !log->ubuf) {
			err = -EINVAL;
			goto errout;
		}
	}

	btf = kzalloc(sizeof(*btf), GFP_KERNEL | __GFP_NOWARN);
	if (!btf) {
		err = -ENOMEM;
		goto errout;
	}

	data = kvmalloc(btf_data_size, GFP_KERNEL | __GFP_NOWARN);
	if (!data) {
		err = -ENOMEM;
		goto errout;
	}

	btf->data = data;
	btf->data_size = btf_data_size;

	if (copy_from_user(data, btf_data, btf_data_size)) {
		err = -EFAULT;
		goto errout;
	}

	env->btf = btf;

	err = btf_parse_hdr(env);
	if (err)
		goto errout;

	err = btf_parse_str_sec(env);
	if (err)
		goto errout;

	err = btf_parse_type_sec(env);
	if (err)
		goto errout;

	if (!err && log->level && bpf_verifier_log_full(log)) {
		err = -ENOSPC;
		goto errout;
	}

	if (!err) {
		btf_verifier_env_free(env);
		return btf;
	}

errout:
	btf_verifier_env_free(env);
	if (btf)
		btf_free(btf);
	return ERR_PTR(err);
}
