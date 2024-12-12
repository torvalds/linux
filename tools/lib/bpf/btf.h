/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2018 Facebook */
/*! \file */

#ifndef __LIBBPF_BTF_H
#define __LIBBPF_BTF_H

#include <stdarg.h>
#include <stdbool.h>
#include <linux/btf.h>
#include <linux/types.h>

#include "libbpf_common.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BTF_ELF_SEC ".BTF"
#define BTF_EXT_ELF_SEC ".BTF.ext"
#define BTF_BASE_ELF_SEC ".BTF.base"
#define MAPS_ELF_SEC ".maps"

struct btf;
struct btf_ext;
struct btf_type;

struct bpf_object;

enum btf_endianness {
	BTF_LITTLE_ENDIAN = 0,
	BTF_BIG_ENDIAN = 1,
};

/**
 * @brief **btf__free()** frees all data of a BTF object
 * @param btf BTF object to free
 */
LIBBPF_API void btf__free(struct btf *btf);

/**
 * @brief **btf__new()** creates a new instance of a BTF object from the raw
 * bytes of an ELF's BTF section
 * @param data raw bytes
 * @param size number of bytes passed in `data`
 * @return new BTF object instance which has to be eventually freed with
 * **btf__free()**
 *
 * On error, error-code-encoded-as-pointer is returned, not a NULL. To extract
 * error code from such a pointer `libbpf_get_error()` should be used. If
 * `libbpf_set_strict_mode(LIBBPF_STRICT_CLEAN_PTRS)` is enabled, NULL is
 * returned on error instead. In both cases thread-local `errno` variable is
 * always set to error code as well.
 */
LIBBPF_API struct btf *btf__new(const void *data, __u32 size);

/**
 * @brief **btf__new_split()** create a new instance of a BTF object from the
 * provided raw data bytes. It takes another BTF instance, **base_btf**, which
 * serves as a base BTF, which is extended by types in a newly created BTF
 * instance
 * @param data raw bytes
 * @param size length of raw bytes
 * @param base_btf the base BTF object
 * @return new BTF object instance which has to be eventually freed with
 * **btf__free()**
 *
 * If *base_btf* is NULL, `btf__new_split()` is equivalent to `btf__new()` and
 * creates non-split BTF.
 *
 * On error, error-code-encoded-as-pointer is returned, not a NULL. To extract
 * error code from such a pointer `libbpf_get_error()` should be used. If
 * `libbpf_set_strict_mode(LIBBPF_STRICT_CLEAN_PTRS)` is enabled, NULL is
 * returned on error instead. In both cases thread-local `errno` variable is
 * always set to error code as well.
 */
LIBBPF_API struct btf *btf__new_split(const void *data, __u32 size, struct btf *base_btf);

/**
 * @brief **btf__new_empty()** creates an empty BTF object.  Use
 * `btf__add_*()` to populate such BTF object.
 * @return new BTF object instance which has to be eventually freed with
 * **btf__free()**
 *
 * On error, error-code-encoded-as-pointer is returned, not a NULL. To extract
 * error code from such a pointer `libbpf_get_error()` should be used. If
 * `libbpf_set_strict_mode(LIBBPF_STRICT_CLEAN_PTRS)` is enabled, NULL is
 * returned on error instead. In both cases thread-local `errno` variable is
 * always set to error code as well.
 */
LIBBPF_API struct btf *btf__new_empty(void);

/**
 * @brief **btf__new_empty_split()** creates an unpopulated BTF object from an
 * ELF BTF section except with a base BTF on top of which split BTF should be
 * based
 * @return new BTF object instance which has to be eventually freed with
 * **btf__free()**
 *
 * If *base_btf* is NULL, `btf__new_empty_split()` is equivalent to
 * `btf__new_empty()` and creates non-split BTF.
 *
 * On error, error-code-encoded-as-pointer is returned, not a NULL. To extract
 * error code from such a pointer `libbpf_get_error()` should be used. If
 * `libbpf_set_strict_mode(LIBBPF_STRICT_CLEAN_PTRS)` is enabled, NULL is
 * returned on error instead. In both cases thread-local `errno` variable is
 * always set to error code as well.
 */
LIBBPF_API struct btf *btf__new_empty_split(struct btf *base_btf);

/**
 * @brief **btf__distill_base()** creates new versions of the split BTF
 * *src_btf* and its base BTF. The new base BTF will only contain the types
 * needed to improve robustness of the split BTF to small changes in base BTF.
 * When that split BTF is loaded against a (possibly changed) base, this
 * distilled base BTF will help update references to that (possibly changed)
 * base BTF.
 *
 * Both the new split and its associated new base BTF must be freed by
 * the caller.
 *
 * If successful, 0 is returned and **new_base_btf** and **new_split_btf**
 * will point at new base/split BTF. Both the new split and its associated
 * new base BTF must be freed by the caller.
 *
 * A negative value is returned on error and the thread-local `errno` variable
 * is set to the error code as well.
 */
LIBBPF_API int btf__distill_base(const struct btf *src_btf, struct btf **new_base_btf,
				 struct btf **new_split_btf);

LIBBPF_API struct btf *btf__parse(const char *path, struct btf_ext **btf_ext);
LIBBPF_API struct btf *btf__parse_split(const char *path, struct btf *base_btf);
LIBBPF_API struct btf *btf__parse_elf(const char *path, struct btf_ext **btf_ext);
LIBBPF_API struct btf *btf__parse_elf_split(const char *path, struct btf *base_btf);
LIBBPF_API struct btf *btf__parse_raw(const char *path);
LIBBPF_API struct btf *btf__parse_raw_split(const char *path, struct btf *base_btf);

LIBBPF_API struct btf *btf__load_vmlinux_btf(void);
LIBBPF_API struct btf *btf__load_module_btf(const char *module_name, struct btf *vmlinux_btf);

LIBBPF_API struct btf *btf__load_from_kernel_by_id(__u32 id);
LIBBPF_API struct btf *btf__load_from_kernel_by_id_split(__u32 id, struct btf *base_btf);

LIBBPF_API int btf__load_into_kernel(struct btf *btf);
LIBBPF_API __s32 btf__find_by_name(const struct btf *btf,
				   const char *type_name);
LIBBPF_API __s32 btf__find_by_name_kind(const struct btf *btf,
					const char *type_name, __u32 kind);
LIBBPF_API __u32 btf__type_cnt(const struct btf *btf);
LIBBPF_API const struct btf *btf__base_btf(const struct btf *btf);
LIBBPF_API const struct btf_type *btf__type_by_id(const struct btf *btf,
						  __u32 id);
LIBBPF_API size_t btf__pointer_size(const struct btf *btf);
LIBBPF_API int btf__set_pointer_size(struct btf *btf, size_t ptr_sz);
LIBBPF_API enum btf_endianness btf__endianness(const struct btf *btf);
LIBBPF_API int btf__set_endianness(struct btf *btf, enum btf_endianness endian);
LIBBPF_API __s64 btf__resolve_size(const struct btf *btf, __u32 type_id);
LIBBPF_API int btf__resolve_type(const struct btf *btf, __u32 type_id);
LIBBPF_API int btf__align_of(const struct btf *btf, __u32 id);
LIBBPF_API int btf__fd(const struct btf *btf);
LIBBPF_API void btf__set_fd(struct btf *btf, int fd);
LIBBPF_API const void *btf__raw_data(const struct btf *btf, __u32 *size);
LIBBPF_API const char *btf__name_by_offset(const struct btf *btf, __u32 offset);
LIBBPF_API const char *btf__str_by_offset(const struct btf *btf, __u32 offset);

LIBBPF_API struct btf_ext *btf_ext__new(const __u8 *data, __u32 size);
LIBBPF_API void btf_ext__free(struct btf_ext *btf_ext);
LIBBPF_API const void *btf_ext__raw_data(const struct btf_ext *btf_ext, __u32 *size);

LIBBPF_API int btf__find_str(struct btf *btf, const char *s);
LIBBPF_API int btf__add_str(struct btf *btf, const char *s);
LIBBPF_API int btf__add_type(struct btf *btf, const struct btf *src_btf,
			     const struct btf_type *src_type);
/**
 * @brief **btf__add_btf()** appends all the BTF types from *src_btf* into *btf*
 * @param btf BTF object which all the BTF types and strings are added to
 * @param src_btf BTF object which all BTF types and referenced strings are copied from
 * @return BTF type ID of the first appended BTF type, or negative error code
 *
 * **btf__add_btf()** can be used to simply and efficiently append the entire
 * contents of one BTF object to another one. All the BTF type data is copied
 * over, all referenced type IDs are adjusted by adding a necessary ID offset.
 * Only strings referenced from BTF types are copied over and deduplicated, so
 * if there were some unused strings in *src_btf*, those won't be copied over,
 * which is consistent with the general string deduplication semantics of BTF
 * writing APIs.
 *
 * If any error is encountered during this process, the contents of *btf* is
 * left intact, which means that **btf__add_btf()** follows the transactional
 * semantics and the operation as a whole is all-or-nothing.
 *
 * *src_btf* has to be non-split BTF, as of now copying types from split BTF
 * is not supported and will result in -ENOTSUP error code returned.
 */
LIBBPF_API int btf__add_btf(struct btf *btf, const struct btf *src_btf);

LIBBPF_API int btf__add_int(struct btf *btf, const char *name, size_t byte_sz, int encoding);
LIBBPF_API int btf__add_float(struct btf *btf, const char *name, size_t byte_sz);
LIBBPF_API int btf__add_ptr(struct btf *btf, int ref_type_id);
LIBBPF_API int btf__add_array(struct btf *btf,
			      int index_type_id, int elem_type_id, __u32 nr_elems);
/* struct/union construction APIs */
LIBBPF_API int btf__add_struct(struct btf *btf, const char *name, __u32 sz);
LIBBPF_API int btf__add_union(struct btf *btf, const char *name, __u32 sz);
LIBBPF_API int btf__add_field(struct btf *btf, const char *name, int field_type_id,
			      __u32 bit_offset, __u32 bit_size);

/* enum construction APIs */
LIBBPF_API int btf__add_enum(struct btf *btf, const char *name, __u32 bytes_sz);
LIBBPF_API int btf__add_enum_value(struct btf *btf, const char *name, __s64 value);
LIBBPF_API int btf__add_enum64(struct btf *btf, const char *name, __u32 bytes_sz, bool is_signed);
LIBBPF_API int btf__add_enum64_value(struct btf *btf, const char *name, __u64 value);

enum btf_fwd_kind {
	BTF_FWD_STRUCT = 0,
	BTF_FWD_UNION = 1,
	BTF_FWD_ENUM = 2,
};

LIBBPF_API int btf__add_fwd(struct btf *btf, const char *name, enum btf_fwd_kind fwd_kind);
LIBBPF_API int btf__add_typedef(struct btf *btf, const char *name, int ref_type_id);
LIBBPF_API int btf__add_volatile(struct btf *btf, int ref_type_id);
LIBBPF_API int btf__add_const(struct btf *btf, int ref_type_id);
LIBBPF_API int btf__add_restrict(struct btf *btf, int ref_type_id);
LIBBPF_API int btf__add_type_tag(struct btf *btf, const char *value, int ref_type_id);

/* func and func_proto construction APIs */
LIBBPF_API int btf__add_func(struct btf *btf, const char *name,
			     enum btf_func_linkage linkage, int proto_type_id);
LIBBPF_API int btf__add_func_proto(struct btf *btf, int ret_type_id);
LIBBPF_API int btf__add_func_param(struct btf *btf, const char *name, int type_id);

/* var & datasec construction APIs */
LIBBPF_API int btf__add_var(struct btf *btf, const char *name, int linkage, int type_id);
LIBBPF_API int btf__add_datasec(struct btf *btf, const char *name, __u32 byte_sz);
LIBBPF_API int btf__add_datasec_var_info(struct btf *btf, int var_type_id,
					 __u32 offset, __u32 byte_sz);

/* tag construction API */
LIBBPF_API int btf__add_decl_tag(struct btf *btf, const char *value, int ref_type_id,
			    int component_idx);

struct btf_dedup_opts {
	size_t sz;
	/* optional .BTF.ext info to dedup along the main BTF info */
	struct btf_ext *btf_ext;
	/* force hash collisions (used for testing) */
	bool force_collisions;
	size_t :0;
};
#define btf_dedup_opts__last_field force_collisions

LIBBPF_API int btf__dedup(struct btf *btf, const struct btf_dedup_opts *opts);

/**
 * @brief **btf__relocate()** will check the split BTF *btf* for references
 * to base BTF kinds, and verify those references are compatible with
 * *base_btf*; if they are, *btf* is adjusted such that is re-parented to
 * *base_btf* and type ids and strings are adjusted to accommodate this.
 *
 * If successful, 0 is returned and **btf** now has **base_btf** as its
 * base.
 *
 * A negative value is returned on error and the thread-local `errno` variable
 * is set to the error code as well.
 */
LIBBPF_API int btf__relocate(struct btf *btf, const struct btf *base_btf);

struct btf_dump;

struct btf_dump_opts {
	size_t sz;
};
#define btf_dump_opts__last_field sz

typedef void (*btf_dump_printf_fn_t)(void *ctx, const char *fmt, va_list args);

LIBBPF_API struct btf_dump *btf_dump__new(const struct btf *btf,
					  btf_dump_printf_fn_t printf_fn,
					  void *ctx,
					  const struct btf_dump_opts *opts);

LIBBPF_API void btf_dump__free(struct btf_dump *d);

LIBBPF_API int btf_dump__dump_type(struct btf_dump *d, __u32 id);

struct btf_dump_emit_type_decl_opts {
	/* size of this struct, for forward/backward compatibility */
	size_t sz;
	/* optional field name for type declaration, e.g.:
	 * - struct my_struct <FNAME>
	 * - void (*<FNAME>)(int)
	 * - char (*<FNAME>)[123]
	 */
	const char *field_name;
	/* extra indentation level (in number of tabs) to emit for multi-line
	 * type declarations (e.g., anonymous struct); applies for lines
	 * starting from the second one (first line is assumed to have
	 * necessary indentation already
	 */
	int indent_level;
	/* strip all the const/volatile/restrict mods */
	bool strip_mods;
	size_t :0;
};
#define btf_dump_emit_type_decl_opts__last_field strip_mods

LIBBPF_API int
btf_dump__emit_type_decl(struct btf_dump *d, __u32 id,
			 const struct btf_dump_emit_type_decl_opts *opts);


struct btf_dump_type_data_opts {
	/* size of this struct, for forward/backward compatibility */
	size_t sz;
	const char *indent_str;
	int indent_level;
	/* below match "show" flags for bpf_show_snprintf() */
	bool compact;		/* no newlines/indentation */
	bool skip_names;	/* skip member/type names */
	bool emit_zeroes;	/* show 0-valued fields */
	size_t :0;
};
#define btf_dump_type_data_opts__last_field emit_zeroes

LIBBPF_API int
btf_dump__dump_type_data(struct btf_dump *d, __u32 id,
			 const void *data, size_t data_sz,
			 const struct btf_dump_type_data_opts *opts);

/*
 * A set of helpers for easier BTF types handling.
 *
 * The inline functions below rely on constants from the kernel headers which
 * may not be available for applications including this header file. To avoid
 * compilation errors, we define all the constants here that were added after
 * the initial introduction of the BTF_KIND* constants.
 */
#ifndef BTF_KIND_FUNC
#define BTF_KIND_FUNC		12	/* Function	*/
#define BTF_KIND_FUNC_PROTO	13	/* Function Proto	*/
#endif
#ifndef BTF_KIND_VAR
#define BTF_KIND_VAR		14	/* Variable	*/
#define BTF_KIND_DATASEC	15	/* Section	*/
#endif
#ifndef BTF_KIND_FLOAT
#define BTF_KIND_FLOAT		16	/* Floating point	*/
#endif
/* The kernel header switched to enums, so the following were never #defined */
#define BTF_KIND_DECL_TAG	17	/* Decl Tag */
#define BTF_KIND_TYPE_TAG	18	/* Type Tag */
#define BTF_KIND_ENUM64		19	/* Enum for up-to 64bit values */

static inline __u16 btf_kind(const struct btf_type *t)
{
	return BTF_INFO_KIND(t->info);
}

static inline __u16 btf_vlen(const struct btf_type *t)
{
	return BTF_INFO_VLEN(t->info);
}

static inline bool btf_kflag(const struct btf_type *t)
{
	return BTF_INFO_KFLAG(t->info);
}

static inline bool btf_is_void(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_UNKN;
}

static inline bool btf_is_int(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_INT;
}

static inline bool btf_is_ptr(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_PTR;
}

static inline bool btf_is_array(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_ARRAY;
}

static inline bool btf_is_struct(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_STRUCT;
}

static inline bool btf_is_union(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_UNION;
}

static inline bool btf_is_composite(const struct btf_type *t)
{
	__u16 kind = btf_kind(t);

	return kind == BTF_KIND_STRUCT || kind == BTF_KIND_UNION;
}

static inline bool btf_is_enum(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_ENUM;
}

static inline bool btf_is_enum64(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_ENUM64;
}

static inline bool btf_is_fwd(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_FWD;
}

static inline bool btf_is_typedef(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_TYPEDEF;
}

static inline bool btf_is_volatile(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_VOLATILE;
}

static inline bool btf_is_const(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_CONST;
}

static inline bool btf_is_restrict(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_RESTRICT;
}

static inline bool btf_is_mod(const struct btf_type *t)
{
	__u16 kind = btf_kind(t);

	return kind == BTF_KIND_VOLATILE ||
	       kind == BTF_KIND_CONST ||
	       kind == BTF_KIND_RESTRICT ||
	       kind == BTF_KIND_TYPE_TAG;
}

static inline bool btf_is_func(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_FUNC;
}

static inline bool btf_is_func_proto(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_FUNC_PROTO;
}

static inline bool btf_is_var(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_VAR;
}

static inline bool btf_is_datasec(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_DATASEC;
}

static inline bool btf_is_float(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_FLOAT;
}

static inline bool btf_is_decl_tag(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_DECL_TAG;
}

static inline bool btf_is_type_tag(const struct btf_type *t)
{
	return btf_kind(t) == BTF_KIND_TYPE_TAG;
}

static inline bool btf_is_any_enum(const struct btf_type *t)
{
	return btf_is_enum(t) || btf_is_enum64(t);
}

static inline bool btf_kind_core_compat(const struct btf_type *t1,
					const struct btf_type *t2)
{
	return btf_kind(t1) == btf_kind(t2) ||
	       (btf_is_any_enum(t1) && btf_is_any_enum(t2));
}

static inline __u8 btf_int_encoding(const struct btf_type *t)
{
	return BTF_INT_ENCODING(*(__u32 *)(t + 1));
}

static inline __u8 btf_int_offset(const struct btf_type *t)
{
	return BTF_INT_OFFSET(*(__u32 *)(t + 1));
}

static inline __u8 btf_int_bits(const struct btf_type *t)
{
	return BTF_INT_BITS(*(__u32 *)(t + 1));
}

static inline struct btf_array *btf_array(const struct btf_type *t)
{
	return (struct btf_array *)(t + 1);
}

static inline struct btf_enum *btf_enum(const struct btf_type *t)
{
	return (struct btf_enum *)(t + 1);
}

struct btf_enum64;

static inline struct btf_enum64 *btf_enum64(const struct btf_type *t)
{
	return (struct btf_enum64 *)(t + 1);
}

static inline __u64 btf_enum64_value(const struct btf_enum64 *e)
{
	/* struct btf_enum64 is introduced in Linux 6.0, which is very
	 * bleeding-edge. Here we are avoiding relying on struct btf_enum64
	 * definition coming from kernel UAPI headers to support wider range
	 * of system-wide kernel headers.
	 *
	 * Given this header can be also included from C++ applications, that
	 * further restricts C tricks we can use (like using compatible
	 * anonymous struct). So just treat struct btf_enum64 as
	 * a three-element array of u32 and access second (lo32) and third
	 * (hi32) elements directly.
	 *
	 * For reference, here is a struct btf_enum64 definition:
	 *
	 * const struct btf_enum64 {
	 *	__u32	name_off;
	 *	__u32	val_lo32;
	 *	__u32	val_hi32;
	 * };
	 */
	const __u32 *e64 = (const __u32 *)e;

	return ((__u64)e64[2] << 32) | e64[1];
}

static inline struct btf_member *btf_members(const struct btf_type *t)
{
	return (struct btf_member *)(t + 1);
}

/* Get bit offset of a member with specified index. */
static inline __u32 btf_member_bit_offset(const struct btf_type *t,
					  __u32 member_idx)
{
	const struct btf_member *m = btf_members(t) + member_idx;
	bool kflag = btf_kflag(t);

	return kflag ? BTF_MEMBER_BIT_OFFSET(m->offset) : m->offset;
}
/*
 * Get bitfield size of a member, assuming t is BTF_KIND_STRUCT or
 * BTF_KIND_UNION. If member is not a bitfield, zero is returned.
 */
static inline __u32 btf_member_bitfield_size(const struct btf_type *t,
					     __u32 member_idx)
{
	const struct btf_member *m = btf_members(t) + member_idx;
	bool kflag = btf_kflag(t);

	return kflag ? BTF_MEMBER_BITFIELD_SIZE(m->offset) : 0;
}

static inline struct btf_param *btf_params(const struct btf_type *t)
{
	return (struct btf_param *)(t + 1);
}

static inline struct btf_var *btf_var(const struct btf_type *t)
{
	return (struct btf_var *)(t + 1);
}

static inline struct btf_var_secinfo *
btf_var_secinfos(const struct btf_type *t)
{
	return (struct btf_var_secinfo *)(t + 1);
}

struct btf_decl_tag;
static inline struct btf_decl_tag *btf_decl_tag(const struct btf_type *t)
{
	return (struct btf_decl_tag *)(t + 1);
}

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBBPF_BTF_H */
