/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */

/*
 * Internal libbpf helpers.
 *
 * Copyright (c) 2019 Facebook
 */

#ifndef __LIBBPF_LIBBPF_INTERNAL_H
#define __LIBBPF_LIBBPF_INTERNAL_H

#include <stdlib.h>
#include <limits.h>

/* make sure libbpf doesn't use kernel-only integer typedefs */
#pragma GCC poison u8 u16 u32 u64 s8 s16 s32 s64

/* prevent accidental re-addition of reallocarray() */
#pragma GCC poison reallocarray

#include "libbpf.h"

#define BTF_INFO_ENC(kind, kind_flag, vlen) \
	((!!(kind_flag) << 31) | ((kind) << 24) | ((vlen) & BTF_MAX_VLEN))
#define BTF_TYPE_ENC(name, info, size_or_type) (name), (info), (size_or_type)
#define BTF_INT_ENC(encoding, bits_offset, nr_bits) \
	((encoding) << 24 | (bits_offset) << 16 | (nr_bits))
#define BTF_TYPE_INT_ENC(name, encoding, bits_offset, bits, sz) \
	BTF_TYPE_ENC(name, BTF_INFO_ENC(BTF_KIND_INT, 0, 0), sz), \
	BTF_INT_ENC(encoding, bits_offset, bits)
#define BTF_MEMBER_ENC(name, type, bits_offset) (name), (type), (bits_offset)
#define BTF_PARAM_ENC(name, type) (name), (type)
#define BTF_VAR_SECINFO_ENC(type, offset, size) (type), (offset), (size)

#ifndef likely
#define likely(x) __builtin_expect(!!(x), 1)
#endif
#ifndef unlikely
#define unlikely(x) __builtin_expect(!!(x), 0)
#endif
#ifndef min
# define min(x, y) ((x) < (y) ? (x) : (y))
#endif
#ifndef max
# define max(x, y) ((x) < (y) ? (y) : (x))
#endif
#ifndef offsetofend
# define offsetofend(TYPE, FIELD) \
	(offsetof(TYPE, FIELD) + sizeof(((TYPE *)0)->FIELD))
#endif

/* Symbol versioning is different between static and shared library.
 * Properly versioned symbols are needed for shared library, but
 * only the symbol of the new version is needed for static library.
 */
#ifdef SHARED
# define COMPAT_VERSION(internal_name, api_name, version) \
	asm(".symver " #internal_name "," #api_name "@" #version);
# define DEFAULT_VERSION(internal_name, api_name, version) \
	asm(".symver " #internal_name "," #api_name "@@" #version);
#else
# define COMPAT_VERSION(internal_name, api_name, version)
# define DEFAULT_VERSION(internal_name, api_name, version) \
	extern typeof(internal_name) api_name \
	__attribute__((alias(#internal_name)));
#endif

extern void libbpf_print(enum libbpf_print_level level,
			 const char *format, ...)
	__attribute__((format(printf, 2, 3)));

#define __pr(level, fmt, ...)	\
do {				\
	libbpf_print(level, "libbpf: " fmt, ##__VA_ARGS__);	\
} while (0)

#define pr_warn(fmt, ...)	__pr(LIBBPF_WARN, fmt, ##__VA_ARGS__)
#define pr_info(fmt, ...)	__pr(LIBBPF_INFO, fmt, ##__VA_ARGS__)
#define pr_debug(fmt, ...)	__pr(LIBBPF_DEBUG, fmt, ##__VA_ARGS__)

#ifndef __has_builtin
#define __has_builtin(x) 0
#endif
/*
 * Re-implement glibc's reallocarray() for libbpf internal-only use.
 * reallocarray(), unfortunately, is not available in all versions of glibc,
 * so requires extra feature detection and using reallocarray() stub from
 * <tools/libc_compat.h> and COMPAT_NEED_REALLOCARRAY. All this complicates
 * build of libbpf unnecessarily and is just a maintenance burden. Instead,
 * it's trivial to implement libbpf-specific internal version and use it
 * throughout libbpf.
 */
static inline void *libbpf_reallocarray(void *ptr, size_t nmemb, size_t size)
{
	size_t total;

#if __has_builtin(__builtin_mul_overflow)
	if (unlikely(__builtin_mul_overflow(nmemb, size, &total)))
		return NULL;
#else
	if (size == 0 || nmemb > ULONG_MAX / size)
		return NULL;
	total = nmemb * size;
#endif
	return realloc(ptr, total);
}

void *btf_add_mem(void **data, size_t *cap_cnt, size_t elem_sz,
		  size_t cur_cnt, size_t max_cnt, size_t add_cnt);

static inline bool libbpf_validate_opts(const char *opts,
					size_t opts_sz, size_t user_sz,
					const char *type_name)
{
	if (user_sz < sizeof(size_t)) {
		pr_warn("%s size (%zu) is too small\n", type_name, user_sz);
		return false;
	}
	if (user_sz > opts_sz) {
		size_t i;

		for (i = opts_sz; i < user_sz; i++) {
			if (opts[i]) {
				pr_warn("%s has non-zero extra bytes\n",
					type_name);
				return false;
			}
		}
	}
	return true;
}

#define OPTS_VALID(opts, type)						      \
	(!(opts) || libbpf_validate_opts((const char *)opts,		      \
					 offsetofend(struct type,	      \
						     type##__last_field),     \
					 (opts)->sz, #type))
#define OPTS_HAS(opts, field) \
	((opts) && opts->sz >= offsetofend(typeof(*(opts)), field))
#define OPTS_GET(opts, field, fallback_value) \
	(OPTS_HAS(opts, field) ? (opts)->field : fallback_value)
#define OPTS_SET(opts, field, value)		\
	do {					\
		if (OPTS_HAS(opts, field))	\
			(opts)->field = value;	\
	} while (0)

int parse_cpu_mask_str(const char *s, bool **mask, int *mask_sz);
int parse_cpu_mask_file(const char *fcpu, bool **mask, int *mask_sz);
int libbpf__load_raw_btf(const char *raw_types, size_t types_len,
			 const char *str_sec, size_t str_len);

int bpf_object__section_size(const struct bpf_object *obj, const char *name,
			     __u32 *size);
int bpf_object__variable_offset(const struct bpf_object *obj, const char *name,
				__u32 *off);

struct btf_ext_info {
	/*
	 * info points to the individual info section (e.g. func_info and
	 * line_info) from the .BTF.ext. It does not include the __u32 rec_size.
	 */
	void *info;
	__u32 rec_size;
	__u32 len;
};

#define for_each_btf_ext_sec(seg, sec)					\
	for (sec = (seg)->info;						\
	     (void *)sec < (seg)->info + (seg)->len;			\
	     sec = (void *)sec + sizeof(struct btf_ext_info_sec) +	\
		   (seg)->rec_size * sec->num_info)

#define for_each_btf_ext_rec(seg, sec, i, rec)				\
	for (i = 0, rec = (void *)&(sec)->data;				\
	     i < (sec)->num_info;					\
	     i++, rec = (void *)rec + (seg)->rec_size)

/*
 * The .BTF.ext ELF section layout defined as
 *   struct btf_ext_header
 *   func_info subsection
 *
 * The func_info subsection layout:
 *   record size for struct bpf_func_info in the func_info subsection
 *   struct btf_sec_func_info for section #1
 *   a list of bpf_func_info records for section #1
 *     where struct bpf_func_info mimics one in include/uapi/linux/bpf.h
 *     but may not be identical
 *   struct btf_sec_func_info for section #2
 *   a list of bpf_func_info records for section #2
 *   ......
 *
 * Note that the bpf_func_info record size in .BTF.ext may not
 * be the same as the one defined in include/uapi/linux/bpf.h.
 * The loader should ensure that record_size meets minimum
 * requirement and pass the record as is to the kernel. The
 * kernel will handle the func_info properly based on its contents.
 */
struct btf_ext_header {
	__u16	magic;
	__u8	version;
	__u8	flags;
	__u32	hdr_len;

	/* All offsets are in bytes relative to the end of this header */
	__u32	func_info_off;
	__u32	func_info_len;
	__u32	line_info_off;
	__u32	line_info_len;

	/* optional part of .BTF.ext header */
	__u32	core_relo_off;
	__u32	core_relo_len;
};

struct btf_ext {
	union {
		struct btf_ext_header *hdr;
		void *data;
	};
	struct btf_ext_info func_info;
	struct btf_ext_info line_info;
	struct btf_ext_info core_relo_info;
	__u32 data_size;
};

struct btf_ext_info_sec {
	__u32	sec_name_off;
	__u32	num_info;
	/* Followed by num_info * record_size number of bytes */
	__u8	data[];
};

/* The minimum bpf_func_info checked by the loader */
struct bpf_func_info_min {
	__u32   insn_off;
	__u32   type_id;
};

/* The minimum bpf_line_info checked by the loader */
struct bpf_line_info_min {
	__u32	insn_off;
	__u32	file_name_off;
	__u32	line_off;
	__u32	line_col;
};

/* bpf_core_relo_kind encodes which aspect of captured field/type/enum value
 * has to be adjusted by relocations.
 */
enum bpf_core_relo_kind {
	BPF_FIELD_BYTE_OFFSET = 0,	/* field byte offset */
	BPF_FIELD_BYTE_SIZE = 1,	/* field size in bytes */
	BPF_FIELD_EXISTS = 2,		/* field existence in target kernel */
	BPF_FIELD_SIGNED = 3,		/* field signedness (0 - unsigned, 1 - signed) */
	BPF_FIELD_LSHIFT_U64 = 4,	/* bitfield-specific left bitshift */
	BPF_FIELD_RSHIFT_U64 = 5,	/* bitfield-specific right bitshift */
	BPF_TYPE_ID_LOCAL = 6,		/* type ID in local BPF object */
	BPF_TYPE_ID_TARGET = 7,		/* type ID in target kernel */
	BPF_TYPE_EXISTS = 8,		/* type existence in target kernel */
	BPF_TYPE_SIZE = 9,		/* type size in bytes */
	BPF_ENUMVAL_EXISTS = 10,	/* enum value existence in target kernel */
	BPF_ENUMVAL_VALUE = 11,		/* enum value integer value */
};

/* The minimum bpf_core_relo checked by the loader
 *
 * CO-RE relocation captures the following data:
 * - insn_off - instruction offset (in bytes) within a BPF program that needs
 *   its insn->imm field to be relocated with actual field info;
 * - type_id - BTF type ID of the "root" (containing) entity of a relocatable
 *   type or field;
 * - access_str_off - offset into corresponding .BTF string section. String
 *   interpretation depends on specific relocation kind:
 *     - for field-based relocations, string encodes an accessed field using
 *     a sequence of field and array indices, separated by colon (:). It's
 *     conceptually very close to LLVM's getelementptr ([0]) instruction's
 *     arguments for identifying offset to a field.
 *     - for type-based relocations, strings is expected to be just "0";
 *     - for enum value-based relocations, string contains an index of enum
 *     value within its enum type;
 *
 * Example to provide a better feel.
 *
 *   struct sample {
 *       int a;
 *       struct {
 *           int b[10];
 *       };
 *   };
 *
 *   struct sample *s = ...;
 *   int x = &s->a;     // encoded as "0:0" (a is field #0)
 *   int y = &s->b[5];  // encoded as "0:1:0:5" (anon struct is field #1, 
 *                      // b is field #0 inside anon struct, accessing elem #5)
 *   int z = &s[10]->b; // encoded as "10:1" (ptr is used as an array)
 *
 * type_id for all relocs in this example  will capture BTF type id of
 * `struct sample`.
 *
 * Such relocation is emitted when using __builtin_preserve_access_index()
 * Clang built-in, passing expression that captures field address, e.g.:
 *
 * bpf_probe_read(&dst, sizeof(dst),
 *		  __builtin_preserve_access_index(&src->a.b.c));
 *
 * In this case Clang will emit field relocation recording necessary data to
 * be able to find offset of embedded `a.b.c` field within `src` struct.
 *
 *   [0] https://llvm.org/docs/LangRef.html#getelementptr-instruction
 */
struct bpf_core_relo {
	__u32   insn_off;
	__u32   type_id;
	__u32   access_str_off;
	enum bpf_core_relo_kind kind;
};

#endif /* __LIBBPF_LIBBPF_INTERNAL_H */
