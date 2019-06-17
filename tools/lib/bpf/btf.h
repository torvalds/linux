/* SPDX-License-Identifier: (LGPL-2.1 OR BSD-2-Clause) */
/* Copyright (c) 2018 Facebook */

#ifndef __LIBBPF_BTF_H
#define __LIBBPF_BTF_H

#include <stdarg.h>
#include <linux/types.h>

#ifdef __cplusplus
extern "C" {
#endif

#ifndef LIBBPF_API
#define LIBBPF_API __attribute__((visibility("default")))
#endif

#define BTF_ELF_SEC ".BTF"
#define BTF_EXT_ELF_SEC ".BTF.ext"
#define MAPS_ELF_SEC ".maps"

struct btf;
struct btf_ext;
struct btf_type;

struct bpf_object;

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
};

LIBBPF_API void btf__free(struct btf *btf);
LIBBPF_API struct btf *btf__new(__u8 *data, __u32 size);
LIBBPF_API struct btf *btf__parse_elf(const char *path,
				      struct btf_ext **btf_ext);
LIBBPF_API int btf__finalize_data(struct bpf_object *obj, struct btf *btf);
LIBBPF_API int btf__load(struct btf *btf);
LIBBPF_API __s32 btf__find_by_name(const struct btf *btf,
				   const char *type_name);
LIBBPF_API __u32 btf__get_nr_types(const struct btf *btf);
LIBBPF_API const struct btf_type *btf__type_by_id(const struct btf *btf,
						  __u32 id);
LIBBPF_API __s64 btf__resolve_size(const struct btf *btf, __u32 type_id);
LIBBPF_API int btf__resolve_type(const struct btf *btf, __u32 type_id);
LIBBPF_API int btf__fd(const struct btf *btf);
LIBBPF_API const void *btf__get_raw_data(const struct btf *btf, __u32 *size);
LIBBPF_API const char *btf__name_by_offset(const struct btf *btf, __u32 offset);
LIBBPF_API int btf__get_from_id(__u32 id, struct btf **btf);
LIBBPF_API int btf__get_map_kv_tids(const struct btf *btf, const char *map_name,
				    __u32 expected_key_size,
				    __u32 expected_value_size,
				    __u32 *key_type_id, __u32 *value_type_id);

LIBBPF_API struct btf_ext *btf_ext__new(__u8 *data, __u32 size);
LIBBPF_API void btf_ext__free(struct btf_ext *btf_ext);
LIBBPF_API const void *btf_ext__get_raw_data(const struct btf_ext *btf_ext,
					     __u32 *size);
LIBBPF_API int btf_ext__reloc_func_info(const struct btf *btf,
					const struct btf_ext *btf_ext,
					const char *sec_name, __u32 insns_cnt,
					void **func_info, __u32 *cnt);
LIBBPF_API int btf_ext__reloc_line_info(const struct btf *btf,
					const struct btf_ext *btf_ext,
					const char *sec_name, __u32 insns_cnt,
					void **line_info, __u32 *cnt);
LIBBPF_API __u32 btf_ext__func_info_rec_size(const struct btf_ext *btf_ext);
LIBBPF_API __u32 btf_ext__line_info_rec_size(const struct btf_ext *btf_ext);

struct btf_dedup_opts {
	unsigned int dedup_table_size;
	bool dont_resolve_fwds;
};

LIBBPF_API int btf__dedup(struct btf *btf, struct btf_ext *btf_ext,
			  const struct btf_dedup_opts *opts);

struct btf_dump;

struct btf_dump_opts {
	void *ctx;
};

typedef void (*btf_dump_printf_fn_t)(void *ctx, const char *fmt, va_list args);

LIBBPF_API struct btf_dump *btf_dump__new(const struct btf *btf,
					  const struct btf_ext *btf_ext,
					  const struct btf_dump_opts *opts,
					  btf_dump_printf_fn_t printf_fn);
LIBBPF_API void btf_dump__free(struct btf_dump *d);

LIBBPF_API int btf_dump__dump_type(struct btf_dump *d, __u32 id);

#ifdef __cplusplus
} /* extern "C" */
#endif

#endif /* __LIBBPF_BTF_H */
